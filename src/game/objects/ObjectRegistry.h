#pragma once

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "config/Config.h"
#include "game/objects/ObjectTypes.h"
#include "gfx/model/ActivityModel.h"

namespace rr {

// Gods98 container types used for object models ("ACT", "LWS", "LWO"/"MESH").
enum class ModelKind { Act, Lws, Lwo };

// The model an object is drawn with: an activity set (.ae folder), an animated scene or a mesh.
struct ObjectModel {
    std::shared_ptr<ActivityModel> activities;
    std::shared_ptr<ActivityModel> secondPart;  // upper part of two-part vehicles (WalkerBody)
    std::shared_ptr<LwsScene> scene;
    std::shared_ptr<Mesh> mesh;
    bool valid() const { return activities || scene || mesh; }
};

struct ObjectTypeInfo {
    ObjType type = ObjType::None;
    int id = 0;
    std::string name;  // config name, e.g. "Pilot", "SmallDigger", "PowerCrystal"
    ObjectModel model;
};

// Object types defined by Lego.cfg (Lego_Load*Types, Lego_LoadMiscObjects), with name lookup
// following Lego_GetObjectByName.
class ObjectRegistry {
public:
    void Load(const Config& cfg, std::string_view gameName);
    // Returns null for unknown names. "TVCamera" resolves to ObjType::TVCamera without a model.
    const ObjectTypeInfo* Find(std::string_view name) const;
    const ObjectTypeInfo* Get(ObjType type, int id) const;
    const ObjectModel* Misc(std::string_view name) const;  // effect models ("BoulderExplode", ...)
    int CountOf(ObjType type) const;

private:
    std::vector<ObjectTypeInfo> types_;
    std::vector<std::pair<std::string, ObjectModel>> misc_;
    ObjectTypeInfo tvCamera_;
};

ObjectRegistry& GetObjectRegistry();

// Loads one container: ACT reads "<path>\<leaf>.ae", LWS "<path>.lws", LWO "<path>.lwo".
// Kinds are tried in order, like the game's chained Container_Load calls.
ObjectModel LoadObjectModel(const std::string& path, std::initializer_list<ModelKind> kinds,
                            const std::string& sharedObjects);

}  // namespace rr
