# Gundam Machine Console

This project is a focused mech preview console. It loads one mech into an empty preview space, plays a manually selected MRB clip, and lets you mount manually selected weapon models with editable transforms and bone bindings. Per-mech profiles save the reviewed parameters, weapon IDs, separate battle/kill-log icon IDs, effect paths, and mount transforms.

The Direct3D executable is `build/Release/GundamMachineConsole.exe`. Select the unpacked `GundamOnline/unpackdat` root, load a mech from `ms_bind.csv`, then use the resource tabs to inspect its model, motion, weapon, and effect resources. Numeric IDs are not converted into effect paths unless the client evidence confirms that relationship.

For an explicit `fx/.../PP_*.vra`, the effect tab can show the first readable `Sprite` image and the source values for dimensions, color, billboard, blend, `anim_type`, and `anim_uv`. Click the existing mount control to add that image as a transformable preview plane in the mech space and save its placement in the profile. This uses a utility alpha blend and one static image; it does not claim to reproduce PP timing, UV animation, client blend behavior, billboard orientation, particle emission, or ID-to-PP bindings.

Profiles are stored beside the executable in `machine_profiles/<mech-id>.ini`. IDs and resource paths with unknown mappings stay manual and are labeled as such. UE experiments under `ue/` are archived and are not part of the active preview workflow. This project does not modify `gundamres` or `iconmatch`.

## Build and verification

```powershell
cmake --build build --config Release --target GundamMachineConsole
ctest --test-dir build -C Release --output-on-failure
```
