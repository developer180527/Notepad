# Architecture

MainWindow

- Owns menus and toolbars
- Coordinates document lifecycle

CanvasView

- Handles zoom and pan
- Displays pages

PageDocumentItem

- Renders paginated text
- Handles cursor and editing

DocumentManager

- Loads and saves .note files
