# srcmap — the field contract with a vpx source tree

`src/utils/SrcFieldMap.h` is generated here. It says, for every part type, which
four character file format tag goes into which key of the part's JSON file in a
vpxtool source tree (`<table>_src`), and how the value is spelled there.

Nothing in it is guessed. Both sides of the contract state the mapping in their
own source, keyed by the same tag:

    Visual Pinball  src/parts/surface.cpp   writer.WriteFloat(FID(HTBT), m_d.m_heightbottom)
    vpin            gameitem/wall.rs        "HTBT" => wall.height_bottom = reader.get_f32()

[vpin](https://github.com/francisdb/vpin) is the library `vpxtool` uses to write
the tree, so its struct field name *is* the JSON key, and the field's Rust type
says how the value is written: an `f32` is a number, a `Color` is `"#rrggbb"`, an
enum is a lowercase name, a quantized byte is expanded back to the value it
means. The generator joins the two, checks that the kinds agree (a bool has to
meet a bool), and reports anything only one side knows about instead of mapping
it.

## Regenerating

    git clone --depth 1 --branch v0.27.0 https://github.com/francisdb/vpin.git /tmp/vpin
    python3 parse_vpin.py /tmp/vpin > /tmp/vpin.json
    python3 gen_srcfieldmap.py ../../src /tmp/vpin.json ../../src/utils/SrcFieldMap.h unmapped.txt

Use the vpin version the tree was extracted with — `vpxtool`'s `Cargo.toml`
names it. `gen_srcfieldmap.py` also writes `unmapped.txt`:
every tag it did not map and why. Most of those are tags vpin still
reads for old files that this Visual Pinball no longer writes, and mesh blobs,
which the tree keeps in `.obj` files beside the part rather than in its JSON.

## Checking it

The mapping is only right if a save that changed nothing rewrites nothing:

    build/VPinballX_BGFX.app/Contents/MacOS/VPinballX_BGFX -SaveToSrc <table>.vpx -Project <copy of the tree>

against a *copy* of a tree should report every part unchanged and leave the
files byte for byte as they were. A tag paired with the wrong key shows up
immediately as a diff. The stronger form of the same check is to scramble every
mapped value in the copy first: the save then has to restore all of them.
