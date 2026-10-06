# Config files (`Lego.cfg` and friends)

Behaviour of `Config_Load` / `Config_FindItem` and the typed getters, as reimplemented in
[`src/config/Config.cpp`](../../src/config/Config.cpp).

## Tokenising

- `;` starts a comment that runs to the end of the line.
- Space, tab, CR and LF separate tokens. Values therefore never contain spaces; display strings use `_`
  where a space should appear.
- Tokens are consumed in pairs, `key value`, producing one item each. Items are kept in file order with
  a nesting **depth**.
- A value of `{` opens a block: the items that follow are one level deeper.
- A lone `}` token closes a block: it lowers the depth for everything after it.

## Lookup

Ids are paths joined with `::` (`Config_BuildStringID`), e.g. `Lego*::Main::StartLevel`.

An item matches an id of *n* parts when:

1. its depth is *n − 1* and its key equals the last part (case-insensitive), and
2. walking backwards, the nearest preceding item at each shallower depth (its enclosing block header)
   matches the corresponding earlier part.

A top-level key containing `*` is a wildcard: `Lego*` matches any first part starting with `Lego`.

Duplicate handling follows from the original loop: lookup normally stops at the **first** match, but if
the top-level block matched through a wildcard it keeps scanning and the **last** match wins. Since
`Lego.cfg` lives entirely inside `Lego* { ... }`, later duplicates override earlier ones.

## Typed getters

| Getter | Behaviour |
| --- | --- |
| string | the raw value token |
| bool (tri-state) | `YES`/`TRUE`/`ON` → true, `NO`/`FALSE`/`OFF` → false, anything else or missing → unset |
| angle | value in degrees, returned in radians (`deg / 360 × 2π`); missing → 0 |
| RGB | `r:g:b` with 0–255 components, returned as 0–1 floats; needs exactly three parts |
| array / next | `FindArray(id)` returns the first child of block `id`; `Next(item)` the next sibling at the same depth, ending at the block's close |
