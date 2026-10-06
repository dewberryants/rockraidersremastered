// LightWave 5 object (LWOB) loader with the game's interpretation of surfaces (Mesh_ParseLWO,
// Mesh_GetSurfInfo) and its ".uv" texture coordinate files (Mesh_UViewMesh).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sstream>

#include "core/Log.h"
#include "fs/FileSystem.h"
#include "fs/Path.h"
#include "gfx/model/Mesh.h"

namespace rr {

namespace {

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok() const { return p <= end; }
    uint16_t U16() {
        if (p + 2 > end) { p = end + 1; return 0; }
        const uint16_t v = uint16_t((p[0] << 8) | p[1]);
        p += 2;
        return v;
    }
    int16_t S16() { return int16_t(U16()); }
    uint32_t U32() {
        if (p + 4 > end) { p = end + 1; return 0; }
        const uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
        p += 4;
        return v;
    }
    float F32() {
        const uint32_t u = U32();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    // NUL-terminated string padded to an even length.
    std::string Str(const uint8_t* limit) {
        const uint8_t* s = p;
        while (p < limit && *p) ++p;
        std::string out(reinterpret_cast<const char*>(s), size_t(p - s));
        if (p < limit) ++p;
        if ((p - s) & 1) ++p;
        return out;
    }
};

struct LwSurface {
    std::string name;
    uint8_t colour[3] = {200, 200, 200};
    uint16_t flags = 0;    // FLAG
    uint16_t texFlags = 0; // TFLG (| 0x80 when the image is a sequence)
    float lumi = 0, tran = 0, diff = 0, refl = 0, spec = 0, glos = 0;
    std::string image;     // TIMG
    Vec3f texSize{1, 1, 1}, texCentre{0, 0, 0};
};

struct LwPolygon {
    std::vector<uint16_t> verts;
    int surface = 0;  // 0-based
};

std::string FileNamePart(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string DirPart(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

void ParseSurface(Reader r, LwSurface& s) {
    const uint8_t* end = r.end;
    s.name = r.Str(end);
    bool inColourTexture = false;
    while (r.p + 6 <= end) {
        char tag[5] = {0};
        std::memcpy(tag, r.p, 4);
        r.p += 4;
        const uint16_t size = r.U16();
        Reader sub{r.p, std::min(r.p + size, end)};
        const std::string t(tag);
        if (t == "COLR" && size >= 3) {
            s.colour[0] = sub.p[0];
            s.colour[1] = sub.p[1];
            s.colour[2] = sub.p[2];
        } else if (t == "FLAG") {
            s.flags = sub.U16();
        } else if (t == "LUMI" || t == "DIFF" || t == "SPEC" || t == "REFL" || t == "TRAN" || t == "GLOS") {
            const float v = float(sub.U16()) / 256.0f;
            if (t == "LUMI") s.lumi = v;
            else if (t == "DIFF") s.diff = v;
            else if (t == "SPEC") s.spec = v;
            else if (t == "REFL") s.refl = v;
            else if (t == "TRAN") s.tran = v;
            else s.glos = v;
        } else if (t == "CTEX" || t == "TTEX") {
            inColourTexture = true;  // the game reads the texture block that follows
        } else if (t == "TIMG" && inColourTexture) {
            std::string img = sub.Str(sub.end);
            if (img != "(none)") {
                const size_t seq = img.find("(sequence)");
                if (seq != std::string::npos) {
                    img = img.substr(0, seq);
                    while (!img.empty() && img.back() == ' ') img.pop_back();
                    s.texFlags |= 0x80;
                }
                s.image = img;
            }
        } else if (t == "TFLG") {
            s.texFlags = uint16_t((sub.U16() & 0x7F) | (s.texFlags & 0x80));
        } else if (t == "TSIZ") {
            s.texSize = {sub.F32(), sub.F32(), sub.F32()};
        } else if (t == "TCTR") {
            s.texCentre = {sub.F32(), sub.F32(), sub.F32()};
        }
        r.p += size + (size & 1);
    }
}

// Texture sequences: "name0000.bmp" -> name0001.bmp ... (at most 100 frames).
std::vector<std::string> SequenceNames(const std::string& first) {
    std::vector<std::string> names;
    const size_t dot = first.find_last_of('.');
    const std::string stem = first.substr(0, dot), ext = dot == std::string::npos ? "" : first.substr(dot);
    size_t digits = stem.size();
    while (digits > 0 && std::isdigit(static_cast<unsigned char>(stem[digits - 1]))) --digits;
    const std::string prefix = stem.substr(0, digits);
    const int width = int(stem.size() - digits);
    int n = width ? std::atoi(stem.c_str() + digits) : 0;
    for (int i = 0; i < 100; ++i, ++n) {
        std::string num = std::to_string(n);
        while (int(num.size()) < width) num = "0" + num;
        names.push_back(prefix + num + ext);
    }
    return names;
}

// ".uv" file: version, surface count, surface names, texture paths, polygon count, then per
// polygon "index count" followed by "u v w" lines.
struct UvData {
    std::vector<std::string> textures;              // per surface (overrides TIMG)
    std::vector<std::vector<Vec2>> polygonCorners;  // per polygon
};

bool LoadUvFile(const std::string& path, size_t polygonCount, UvData& out) {
    auto text = Fs().ReadText(path);
    if (!text) return false;
    std::istringstream in(*text);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    if (lines.size() < 2) return false;
    const int surfaces = std::atoi(lines[1].c_str());
    size_t i = 2 + size_t(surfaces);
    for (int s = 0; s < surfaces && i < lines.size(); ++s, ++i) out.textures.push_back(lines[i]);
    if (i >= lines.size()) return false;
    ++i;  // polygon count
    out.polygonCorners.assign(polygonCount, {});
    while (i < lines.size()) {
        int poly = 0, count = 0;
        if (std::sscanf(lines[i].c_str(), "%d %d", &poly, &count) != 2) break;
        ++i;
        std::vector<Vec2> corners;
        for (int c = 0; c < count && i < lines.size(); ++c, ++i) {
            float u = 0, v = 0;
            std::sscanf(lines[i].c_str(), "%f %f", &u, &v);
            corners.push_back({u, -v});
        }
        if (poly >= 0 && size_t(poly) < polygonCount) out.polygonCorners[size_t(poly)] = std::move(corners);
    }
    return true;
}

}  // namespace

// --- Texture cache ------------------------------------------------------------------------

MeshTextureCache& GetMeshTextures() {
    static MeshTextureCache instance;
    return instance;
}

const Texture3D* MeshTextureCache::LoadExact(const std::string& path, const std::string& fileName) {
    const std::string key = NormalizeGamePath(path);
    for (auto& [k, tex] : cache_)
        if (k == key) return tex.get();
    if (!Fs().Exists(path)) return nullptr;
    auto bmp = LoadBmp(path);
    std::unique_ptr<Texture3D> tex;
    if (bmp) {
        // "A<nnn>_name": palette entry nnn is transparent.
        const std::string& n = fileName;
        if (bmp->paletted() && n.size() > 4 && (n[0] == 'a' || n[0] == 'A') && std::isdigit((unsigned char)n[1]) &&
            std::isdigit((unsigned char)n[2]) && std::isdigit((unsigned char)n[3]) && n[4] == '_') {
            const int key = std::atoi(n.substr(1, 3).c_str());
            if (key >= 0 && key < 256) {
                for (size_t i = 0; i < bmp->indices.size(); ++i)
                    if (bmp->indices[i] == key) bmp->rgba[i * 4 + 3] = 0;
            }
            tex = Texture3D::FromBitmap(*bmp, false);
        } else {
            tex = Texture3D::FromBitmap(*bmp, false);
        }
    }
    const Texture3D* raw = tex.get();
    cache_.emplace_back(key, std::move(tex));
    return raw;
}

const Texture3D* MeshTextureCache::Load(const std::string& dir, const std::string& fileName) {
    if (fileName.empty()) return nullptr;
    const std::string local = dir.empty() ? fileName : dir + "\\" + fileName;
    if (const Texture3D* t = LoadExact(local, fileName)) return t;
    if (!sharedDir_.empty()) return LoadExact(sharedDir_ + "\\" + fileName, fileName);
    return nullptr;
}

// --- LWO ----------------------------------------------------------------------------------

std::shared_ptr<Mesh> LoadLwo(const std::string& pathIn) {
    std::string path = pathIn;
    if (path.size() < 4 || !EqualsNoCase(std::string_view(path).substr(path.size() - 4), ".lwo")) path += ".lwo";
    auto bytes = Fs().ReadAll(path);
    if (!bytes || bytes->size() < 12 || std::memcmp(bytes->data(), "FORM", 4) != 0 ||
        std::memcmp(bytes->data() + 8, "LWOB", 4) != 0) {
        RR_DEBUG("LWO %s missing or not LWOB", path.c_str());
        return nullptr;
    }
    const uint8_t* base = bytes->data();
    Reader r{base + 12, base + bytes->size()};
    std::vector<Vec3f> points;
    std::vector<std::string> surfaceNames;
    std::vector<LwSurface> surfaces;
    std::vector<LwPolygon> polygons;
    while (r.p + 8 <= r.end) {
        char tag[5] = {0};
        std::memcpy(tag, r.p, 4);
        r.p += 4;
        const uint32_t size = r.U32();
        const uint8_t* chunkEnd = std::min(r.p + size, r.end);
        Reader c{r.p, chunkEnd};
        const std::string t(tag);
        if (t == "PNTS") {
            while (c.p + 12 <= chunkEnd) points.push_back({c.F32(), c.F32(), c.F32()});
        } else if (t == "SRFS") {
            while (c.p < chunkEnd) surfaceNames.push_back(c.Str(chunkEnd));
        } else if (t == "POLS") {
            while (c.p + 2 <= chunkEnd) {
                LwPolygon poly;
                const uint16_t n = c.U16();
                for (uint16_t i = 0; i < n; ++i) poly.verts.push_back(c.U16());
                int16_t surf = c.S16();
                if (surf < 0) {  // detail polygons follow; the game ignores them
                    surf = int16_t(-surf);
                    const uint16_t details = c.U16();
                    for (uint16_t d = 0; d < details && c.p < chunkEnd; ++d) {
                        const uint16_t dn = c.U16();
                        c.p += dn * 2 + 2;
                    }
                }
                poly.surface = std::max(0, surf - 1);
                if (!c.ok()) break;
                polygons.push_back(std::move(poly));
            }
        } else if (t == "SURF") {
            LwSurface s;
            ParseSurface(c, s);
            surfaces.push_back(s);
        }
        r.p += size + (size & 1);
    }

    // Surfaces in SRFS order; unnamed ones become a white "Default".
    std::vector<LwSurface> ordered;
    for (const std::string& n : surfaceNames) {
        auto it = std::find_if(surfaces.begin(), surfaces.end(), [&](const LwSurface& s) { return EqualsNoCase(s.name, n); });
        if (it != surfaces.end()) {
            ordered.push_back(*it);
        } else {
            LwSurface d;
            d.name = "Default";
            d.colour[0] = d.colour[1] = d.colour[2] = 255;
            ordered.push_back(d);
        }
    }
    if (ordered.empty()) ordered.push_back(LwSurface{});

    const std::string dir = DirPart(path);
    const std::string stem = path.substr(0, path.size() - 4);
    UvData uv;
    const bool haveUv = LoadUvFile(stem + ".uv", polygons.size(), uv);

    auto mesh = std::make_shared<Mesh>();
    mesh->path = path;
    mesh->groups.resize(ordered.size());

    // Polygon plane normals (from the first three vertices).
    std::vector<Vec3f> planeNormals(polygons.size());
    for (size_t i = 0; i < polygons.size(); ++i) {
        const auto& v = polygons[i].verts;
        if (v.size() < 3 || v[0] >= points.size() || v[1] >= points.size() || v[2] >= points.size()) continue;
        planeNormals[i] = Normalize(Cross(points[v[1]] - points[v[0]], points[v[2]] - points[v[0]]));
    }

    for (size_t s = 0; s < ordered.size(); ++s) {
        const LwSurface& surf = ordered[s];
        MeshGroup& g = mesh->groups[s];
        // Smooth normals: sum of the plane normals of this surface's polygons using each point.
        std::vector<Vec3f> normalSum(points.size(), Vec3f{});
        for (size_t i = 0; i < polygons.size(); ++i) {
            if (polygons[i].surface != int(s)) continue;
            for (uint16_t vi : polygons[i].verts)
                if (vi < points.size()) normalSum[vi] += planeNormals[i];
        }
        auto planarUv = [&](const Vec3f& p) {
            Vec2 t;
            t.x = ((surf.texFlags & 1) ? (p.z - surf.texCentre.z) / surf.texSize.z : (p.x - surf.texCentre.x) / surf.texSize.x) + 0.5f;
            t.y = ((surf.texFlags & 2) ? (p.z - surf.texCentre.z) / surf.texSize.z : (p.y - surf.texCentre.y) / surf.texSize.y) + 0.5f;
            return t;
        };
        for (size_t i = 0; i < polygons.size(); ++i) {
            const LwPolygon& poly = polygons[i];
            if (poly.surface != int(s) || poly.verts.size() < 3) continue;
            const auto* corners = haveUv && i < uv.polygonCorners.size() ? &uv.polygonCorners[i] : nullptr;
            const uint32_t first = uint32_t(g.vertices.size());
            for (size_t k = 0; k < poly.verts.size(); ++k) {
                const uint16_t vi = poly.verts[k];
                const Vec3f p = vi < points.size() ? points[vi] : Vec3f{};
                Vertex3D v;
                v.pos = p;
                v.normal = Normalize(normalSum[vi < points.size() ? vi : 0]);
                v.uv = (corners && k < corners->size()) ? (*corners)[k] : planarUv(p);
                g.vertices.push_back(v);
            }
            // Triangles (0,1,2) and for quads (0,2,3); larger polygons are fanned.
            for (size_t k = 1; k + 1 < poly.verts.size(); ++k) {
                g.indices.push_back(first);
                g.indices.push_back(first + uint32_t(k));
                g.indices.push_back(first + uint32_t(k + 1));
            }
        }

        // Material (Mesh_GetSurfInfo + Mesh_ParseLWO group setup).
        MeshMaterial& m = g.material;
        Vec3f colour{surf.colour[0] / 256.0f, surf.colour[1] / 256.0f, surf.colour[2] / 256.0f};
        std::string image = surf.image;
        if (haveUv && s < uv.textures.size()) image = uv.textures[s];
        if (!image.empty()) {
            const std::string file = FileNamePart(image);
            if (surf.texFlags & 0x80) {
                for (const std::string& frame : SequenceNames(file)) {
                    const Texture3D* t = GetMeshTextures().Load(dir, frame);
                    if (!t) break;
                    m.sequence.push_back(t);
                }
                m.texture = m.sequence.empty() ? nullptr : m.sequence[0];
            } else {
                m.texture = GetMeshTextures().Load(dir, file);
            }
            if (!(surf.flags & 0x80)) colour = {1, 1, 1};
        }
        m.diffuse = colour * surf.diff;
        m.emissive = colour * surf.lumi;
        m.specular = (surf.flags & 8) ? colour * surf.spec : Vec3f{surf.spec, surf.spec, surf.spec};
        m.power = surf.glos * 10.0f;
        m.alpha = 1.0f - surf.tran;
        m.additive = (surf.flags & 0x200) != 0;
        m.doubleSided = (surf.flags & 0x100) != 0;
        m.pointFilter = !(surf.texFlags & 0x20);
    }
    // Drop empty groups.
    mesh->groups.erase(std::remove_if(mesh->groups.begin(), mesh->groups.end(),
                                      [](const MeshGroup& g) { return g.indices.empty(); }),
                       mesh->groups.end());
    return mesh;
}

}  // namespace rr
