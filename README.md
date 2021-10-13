# poe-utils
Miscellaneous data mangling utilities for Path of Exile.

## process-image
Replacement tool for ImageMagick to convert from many DDS pixel formats to PNG with an optional crop to a target region.

Usage:
```
process-image convert input.dds output.png [x y w h]
```

### Examples
Convert a whole image to PNG:
```bash
process-image convert "Art/2DItems/Gems/SoulfeastGem.dds" "Forbidden Rite Gem.png"
```

Slice out a smaller region:
```bash
process-image convert "Art/Textures/Interface/2D/2DArt_UIImages_InGame_4K_4.dds" "WorldPanelMapAct2.png" 8 8 1218 770
```

Note that the region is given as an origin and a size, unlike the start point and end point given in files like `UIImages1.txt`.
