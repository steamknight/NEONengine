# Inventory UI Prototype — Layout Wireframe

## Intent

Rough, implementation-ready layout contract for the full-screen inventory UI after removing the side description panel.

## Regions

- Outer margin: 8px on all edges.
- Title row at top.
- Hover-name one-line strip directly below title.
- Central grid region (horizontal flow, wrapping to rows).
- Bottom control strip with Prev / page indicator / Next / Close.

## Wireframe (not to scale)

```text
+--------------------------------------------------------------+
| Inventory                                                    |
|--------------------------------------------------------------|
| Hover: <item name>                                           |
|--------------------------------------------------------------|
| [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ]             |
| [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ]             |
| [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ]             |
|                                                              |
|                                                              |
|                                                              |
|--------------------------------------------------------------|
| Prev        Page 1 / N             Next              Close   |
+--------------------------------------------------------------+
```

## Interaction notes

- Hover updates the one-line name strip.
- Single-click opens the dismissible description popup.
- Focused cell uses two colors in UI palette band 32–63 with palette cycling ("marching ants").
- Pagination is non-wrapping; first/last page disables Prev/Next respectively.

## Geometry rules

- Cell visual content uses authored inventory frame size unscaled.
- Cell box = frame size + fixed padding for focus affordance.
- Items per page = `floor(gridWidth / cellWidth) * floor(gridHeight / cellHeight)`.
- Spacing rhythm: 8px outer/major, 4px minor, matching existing UI conventions.
