# EpicEngineAIAccessBridge Plugin

Editor-only UE 5.7 plugin that exposes a localhost HTTP API for programmatic Blueprint inspection and patching. Starts automatically when the editor loads — no UI or menu items.

## Server

- **Port:** 8523 (localhost only)
- **Auto-starts** on editor launch, stops on shutdown
- **Auth:** Token-based. Call `/session/open` first to get a bearer token
- **Audit log:** `Saved/Logs/EpicEngineAIAccessBridge.log`

## Authentication

All endpoints except `/session/open` require an `Authorization: Bearer <token>` header.

- Tokens expire after **1 hour**
- Maximum **5 concurrent sessions**
- Expired sessions are pruned automatically

### Get a token

```bash
curl -X POST http://localhost:8523/session/open \
  -H "Content-Type: application/json" -d '{}'
```

Returns: `{ "token": "...", "expiresAt": "..." }`

## API Endpoints

### POST /bp/inspect

Read the full structure of a Blueprint graph as JSON.

**Request body:**
```json
{
  "assetPath": "/Game/Blueprints/BP_MyActor",
  "graph": "EventGraph"
}
```

- `graph` defaults to `"UserConstructionScript"` if omitted
- `"ConstructionScript"` is auto-normalized to `"UserConstructionScript"`
- Fuzzy-matches graph names if exact match isn't found
- Lists available graphs in the error message if none match

**Response:** JSON with `nodes[]`, `links[]`, and `nodeCount`. Each node includes:
- `id`, `class`, `title`, `x`, `y`, `comment`
- `pins[]` — each with `id`, `displayName`, `direction`, `category`, `subType`, `defaultValue`, `connected`, `linkCount`, `linkedTo[]`

---

### POST /bp/graphs

List all graphs in a Blueprint.

**Request body:**
```json
{
  "assetPath": "/Game/Blueprints/BP_MyActor"
}
```

**Response:**
```json
{
  "assetPath": "/Game/Blueprints/BP_MyActor",
  "count": 3,
  "graphs": [
    { "name": "EventGraph", "class": "EdGraph", "nodeCount": 12, "category": "eventGraph" },
    { "name": "UserConstructionScript", "class": "EdGraph", "nodeCount": 1, "category": "function" },
    { "name": "MyFunction", "class": "EdGraph", "nodeCount": 5, "category": "function" }
  ]
}
```

Categories: `eventGraph`, `function`, `macro`, `other`

---

### POST /bp/classes

Query available functions and properties on a UClass.

**Request body:**
```json
{
  "className": "ReapAndRuinCharacter",
  "filter": "blueprintCallable"
}
```

- `className` — UE class name (tries with/without U/A prefix automatically)
- `filter` — optional: `"blueprintCallable"` (functions only) or `"blueprintVisible"` (properties only)

**Response:**
```json
{
  "class": "ReapAndRuinCharacter",
  "parent": "Character",
  "functions": [
    {
      "name": "GetInventoryComponent",
      "owner": "ReapAndRuinCharacter",
      "isStatic": false,
      "isBlueprintCallable": true,
      "isPure": true,
      "parameters": [
        { "name": "ReturnValue", "type": "UInventoryComponent*", "isReturn": true, "isOutput": true }
      ]
    }
  ],
  "properties": [
    {
      "name": "InventoryComponent",
      "type": "TObjectPtr<UInventoryComponent>",
      "owner": "ReapAndRuinCharacter",
      "isBlueprintVisible": true,
      "isBlueprintReadOnly": true
    }
  ]
}
```

---

### POST /bp/patch

Dry-run validation of patch operations. Does NOT modify anything.

**Request body:**
```json
{
  "assetPath": "/Game/Blueprints/BP_MyActor",
  "graph": "EventGraph",
  "dryRun": true,
  "ops": [ ... ]
}
```

- `dryRun` must be `true` (enforced — use `/bp/apply` to execute)
- Max **100 ops** per request

**Response:** Per-op validation results with `status` ("ok" / "warning" / "error"), plus summary fields `totalOps`, `errors`, `warnings`, `canApply`.

---

### POST /bp/apply

Apply patch operations, compile, and save the Blueprint. **Atomic** — if any op fails, all changes are rolled back.

**Request body:**
```json
{
  "assetPath": "/Game/Blueprints/BP_MyActor",
  "graph": "EventGraph",
  "ops": [ ... ]
}
```

- Creates an automatic backup before applying (stored in `Saved/EpicEngineAIAccessBridge/Backups/`)
- If any op fails, all changes are rolled back via UE transaction system
- Compiles the Blueprint after applying all ops (skipped on rollback)
- Saves the package to disk
- Max **100 ops** per request
- Backups are pruned automatically (max 10 per asset, max 7 days old)

**Response:** Per-op results, plus `succeeded`, `failed`, `rolledBack`, `compiled`, `success` (true only if 0 failures and compilation succeeded).

---

### POST /bp/undo

Revert the last `/bp/apply` for a given asset. Supports multi-level undo (up to 10 levels).

**Request body:**
```json
{
  "assetPath": "/Game/Blueprints/BP_MyActor",
  "levels": 1
}
```

- `levels` — optional, defaults to 1. How many undo levels to revert.
- Returns the number of levels actually undone and remaining.

**Response:**
```json
{
  "success": true,
  "assetPath": "/Game/Blueprints/BP_MyActor",
  "levelsUndone": 1,
  "levelsRemaining": 3,
  "message": "Reverted 1 level(s)."
}
```

---

## Patch Operations

All operations go in the `ops` array. Seven operation types are supported:

### addNode

Add a Blueprint node to the graph.

```json
{
  "op": "addNode",
  "class": "K2Node_CallFunction",
  "id": "myNode1",
  "x": 200,
  "y": 100
}
```

- `id` is a user-defined label used to reference this node in subsequent ops
- `x`, `y` set the node's position in the graph editor

**Specialized configuration** — these optional fields configure the node before pin allocation, so the node gets the correct pins:

For `K2Node_CallFunction`:
```json
{
  "op": "addNode",
  "class": "K2Node_CallFunction",
  "id": "printNode",
  "x": 300,
  "y": 200,
  "functionReference": "KismetSystemLibrary.PrintString"
}
```
- `functionReference` — `"ClassName.FunctionName"`. The class is resolved with/without U/A prefix.

For `K2Node_VariableGet` / `K2Node_VariableSet`:
```json
{
  "op": "addNode",
  "class": "K2Node_VariableGet",
  "id": "getInv",
  "x": 300,
  "y": 200,
  "variableReference": "InventoryComponent"
}
```
- `variableReference` — `"PropertyName"` (searches BP's generated class) or `"ClassName.PropertyName"`.

For `K2Node_DynamicCast`:
```json
{
  "op": "addNode",
  "class": "K2Node_DynamicCast",
  "id": "castNode",
  "x": 300,
  "y": 200,
  "castClass": "ReapAndRuinCharacter"
}
```
- `castClass` — target class name (resolved with/without A/U prefix).

### removeNode

Remove a node from the graph. All connections are broken automatically.

```json
{
  "op": "removeNode",
  "targetNode": "K2Node_CallFunction_4"
}
```

### moveNode

Reposition a node in the graph editor.

```json
{
  "op": "moveNode",
  "targetNode": "K2Node_CallFunction_0",
  "x": 500,
  "y": 300
}
```

### connect

Connect two pins.

```json
{
  "op": "connect",
  "from": "myNode1.execOut",
  "to": "myNode2.execIn"
}
```

- Pin references use format `nodeId.pinName`
- Validates pin direction compatibility and schema rules before connecting

### disconnect

Break a connection between two pins.

```json
{
  "op": "disconnect",
  "from": "nodeId.pinName",
  "to": "nodeId.pinName"
}
```

### setDefault

Set a pin's default value. Validates the value against the pin's type.

```json
{
  "op": "setDefault",
  "targetNode": "myNode1",
  "pin": "Priority",
  "value": "5"
}
```

**Type validation:**
- `int` pins — value must be numeric
- `float`/`double` pins — value must be numeric
- `bool` pins — value must be `true`, `false`, `0`, or `1`
- `exec` pins — cannot have a default value (error)
- `string`/`name`/`text` pins — any value accepted
- `struct` pins — accepted as-is (use UE struct literal format)

### setComment

Set or clear a node's comment bubble.

```json
{
  "op": "setComment",
  "targetNode": "myNode1",
  "comment": "This handles the inventory lookup"
}
```

- Setting an empty string clears and hides the comment bubble.

---

## Pin Name Aliases

These aliases are automatically resolved:

| Alias         | Resolves to                  |
|---------------|------------------------------|
| `execIn`      | `execute` (PN_Execute)       |
| `execOut`     | `then` (PN_Then)             |
| `then`        | `then` (PN_Then)             |
| `condition`   | `Condition` (PN_Condition)   |
| `returnValue` | `ReturnValue` (PN_ReturnValue) |

Pin name matching is case-insensitive as a fallback. If a pin isn't found, the error message lists all available pins on the node.

## Allowlisted Node Classes

Only these K2Node types can be created via `addNode`:

- `K2Node_CallFunction` — function calls (use `functionReference` for proper pin setup)
- `K2Node_IfThenElse` — branch nodes
- `K2Node_VariableGet` — variable getters (use `variableReference` for proper pin setup)
- `K2Node_VariableSet` — variable setters (use `variableReference` for proper pin setup)
- `K2Node_DynamicCast` — cast nodes (use `castClass` for proper pin setup)
- `K2Node_MakeArray` — make array
- `K2Node_BreakStruct` — break struct
- `K2Node_MakeStruct` — make struct
- `K2Node_Knot` — reroute nodes
- `K2Node_IsValidObject` — IsValid check
- `K2Node_MacroInstance` — macro instances
- `K2Node_TemporaryVariable` — temporary variables

## Node Referencing

When using `/bp/inspect`, existing nodes are identified by their UE internal name (e.g., `K2Node_CallFunction_0`). When using `addNode`, you assign your own `id` which can then be used in subsequent `connect`, `disconnect`, and `setDefault` ops within the same request.

## Atomic Rollback

If any operation in an `/bp/apply` request fails, **all changes are automatically rolled back** using the Unreal Engine transaction system. The response will include `"rolledBack": true` and indicate which op failed.

## Multi-Level Undo

Each `/bp/apply` creates a backup. Up to **10 backup levels** are stored per asset. Use `/bp/undo` with `"levels": N` to undo multiple applies. Backups older than **7 days** are automatically pruned.

## Scene Manipulation Endpoints

### POST /scene/actors

List all actors in the current editor level.

**Request body:**
```json
{
  "classFilter": "StaticMeshActor"
}
```

- `classFilter` — optional, substring match on class name

**Response:** `{ "count": N, "actors": [...] }` — each actor includes `name`, `label`, `class`, `isHidden`, `transform`, and `components[]`.

---

### POST /scene/find

Find actors by name or label (substring match).

**Request body:**
```json
{
  "name": "Tree",
  "classFilter": "BP_Tree"
}
```

**Response:** Same format as `/scene/actors`.

---

### POST /scene/spawn

Spawn an actor by class name or Blueprint asset path.

**Request body:**
```json
{
  "className": "StaticMeshActor",
  "assetPath": "/Game/Blueprint/BP_Tree",
  "label": "MyTree_01",
  "location": { "x": 100, "y": 200, "z": 0 },
  "rotation": { "pitch": 0, "yaw": 45, "roll": 0 },
  "scale": { "x": 1, "y": 1, "z": 1 }
}
```

- Provide either `className` (native class) or `assetPath` (Blueprint), not both
- `label`, `location`, `rotation`, `scale` are all optional

**Response:** The spawned actor's full serialization (name, label, class, transform, components).

---

### POST /scene/delete

Delete an actor by name or label.

**Request body:**
```json
{
  "name": "MyTree_01"
}
```

**Response:** `{ "success": true, "deleted": "MyTree_01", "class": "BP_Tree_C" }`

---

### POST /scene/transform

Get or set an actor's transform. If `location`/`rotation`/`scale` fields are present, sets them. Otherwise returns the current transform.

**Get transform:**
```json
{
  "name": "MyTree_01"
}
```

**Set transform:**
```json
{
  "name": "MyTree_01",
  "location": { "x": 500, "y": 300, "z": 0 },
  "rotation": { "pitch": 0, "yaw": 90, "roll": 0 },
  "scale": { "x": 2, "y": 2, "z": 2 }
}
```

- You can provide any combination of `location`, `rotation`, `scale` — only provided fields are updated.

---

### POST /scene/properties

Get or set properties on an actor. If `property` and `value` are present, sets the property. Otherwise returns all editable properties.

**Get properties:**
```json
{
  "name": "MyTree_01"
}
```

**Set property:**
```json
{
  "name": "MyTree_01",
  "property": "bHidden",
  "value": "true"
}
```

---

### POST /scene/create-blueprint

Create a new Blueprint asset in the project's content directory.

**Request body:**
```json
{
  "parentClass": "Actor",
  "path": "/Game/Blueprints",
  "name": "BP_MyNewActor"
}
```

| Field | Required | Description |
|---|---|---|
| `parentClass` | yes | UE class name to inherit from (e.g. `"Actor"`, `"Pawn"`, `"Character"`). Resolved with/without `A`/`U` prefix automatically. |
| `path` | yes | Content-browser folder path. Must start with `/Game/`. |
| `name` | yes | Asset name. Alphanumeric and underscores only. |

**Response:**
```json
{
  "success": true,
  "name": "BP_MyNewActor",
  "path": "/Game/Blueprints",
  "fullPath": "/Game/Blueprints/BP_MyNewActor",
  "parentClass": "Actor",
  "generatedClass": "BP_MyNewActor_C"
}
```

**Error cases:**
- `400` — Missing or invalid `parentClass`, `path`, or `name`; name contains illegal characters; path does not start with `/Game/`
- `400` — Blueprint with that name already exists at that path
- `500` — Asset creation failed (parent class not found or AssetTools failure)

**Sample curl:**
```bash
curl -s -X POST http://localhost:8523/scene/create-blueprint \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "parentClass": "Actor",
    "path": "/Game/Blueprints",
    "name": "BP_MyNewActor"
  }'
```

---

## Python Scripting

### POST /exec/python

Execute Python code in the Unreal Editor's Python environment. Requires the **Python Editor Script Plugin** to be enabled.

**Request body:**
```json
{
  "code": "print(unreal.EditorLevelLibrary.get_all_level_actors())"
}
```

**Response:**
```json
{
  "success": true,
  "output": "[StaticMeshActor'...', PointLight'...']",
  "lineCount": 1
}
```

This is an escape hatch — anything UE's Python API supports can be done through this endpoint, without needing a dedicated route.

---

## File Structure

```
Plugins/EpicEngineAIAccessBridge/
├── EpicEngineAIAccessBridge.uplugin
└── Source/EpicEngineAIAccessBridge/
    ├── EpicEngineAIAccessBridge.Build.cs
    ├── Private/
    │   ├── EpicEngineAIAccessBridgeModule.cpp    (module startup/shutdown)
    │   ├── BerniHttpServer.cpp            (HTTP routes and auth)
    │   ├── BerniGraphOps.cpp              (Blueprint inspect/validate/apply/undo)
    │   ├── BerniSceneOps.cpp              (scene/actor manipulation + Python exec)
    │   └── BerniAuditLog.cpp              (structured file logging)
    └── Public/
        ├── EpicEngineAIAccessBridgeModule.h
        ├── BerniHttpServer.h
        ├── BerniGraphOps.h
        ├── BerniSceneOps.h
        ├── BerniAuditLog.h
        └── BerniTypes.h                   (session, op structs, constants, allowlist)
```

## Example Workflow

```bash
# 1. Open a session
TOKEN=$(curl -s -X POST http://localhost:8523/session/open -H "Content-Type: application/json" -d '{}' | jq -r '.token')

# 2. List available graphs
curl -s -X POST http://localhost:8523/bp/graphs \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"assetPath": "/Game/Blueprint/BP_ReapController"}'

# 3. Query a class to find available functions/properties
curl -s -X POST http://localhost:8523/bp/classes \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"className": "ReapAndRuinCharacter", "filter": "blueprintCallable"}'

# 4. Inspect a Blueprint graph
curl -s -X POST http://localhost:8523/bp/inspect \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"assetPath": "/Game/Blueprint/BP_ReapController", "graph": "EventGraph"}'

# 5. Validate a patch (dry-run)
curl -s -X POST http://localhost:8523/bp/patch \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "assetPath": "/Game/Blueprint/BP_ReapController",
    "graph": "EventGraph",
    "dryRun": true,
    "ops": [
      {"op": "removeNode", "targetNode": "K2Node_CallFunction_4"},
      {"op": "addNode", "class": "K2Node_VariableGet", "id": "getInv", "x": 400, "y": 550, "variableReference": "ReapAndRuinCharacter.InventoryComponent"},
      {"op": "connect", "from": "K2Node_DynamicCast_0.AsPlayerCharacter", "to": "getInv.self"},
      {"op": "connect", "from": "getInv.InventoryComponent", "to": "K2Node_CallFunction_1.self"}
    ]
  }'

# 6. Apply the patch (atomic — rolls back on any failure)
curl -s -X POST http://localhost:8523/bp/apply \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "assetPath": "/Game/Blueprint/BP_ReapController",
    "graph": "EventGraph",
    "ops": [
      {"op": "removeNode", "targetNode": "K2Node_CallFunction_4"},
      {"op": "addNode", "class": "K2Node_VariableGet", "id": "getInv", "x": 400, "y": 550, "variableReference": "ReapAndRuinCharacter.InventoryComponent"},
      {"op": "connect", "from": "K2Node_DynamicCast_0.AsPlayerCharacter", "to": "getInv.self"},
      {"op": "connect", "from": "getInv.InventoryComponent", "to": "K2Node_CallFunction_1.self"}
    ]
  }'

# 7. Undo if needed (supports multi-level)
curl -s -X POST http://localhost:8523/bp/undo \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"assetPath": "/Game/Blueprint/BP_ReapController", "levels": 1}'

# 8. List all actors in the scene
curl -s -X POST http://localhost:8523/scene/actors \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{}'

# 9. Spawn a Blueprint actor
curl -s -X POST http://localhost:8523/scene/spawn \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"assetPath": "/Game/Blueprint/BP_Tree", "label": "TestTree", "location": {"x": 100, "y": 200, "z": 0}}'

# 10. Move an actor
curl -s -X POST http://localhost:8523/scene/transform \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"name": "TestTree", "location": {"x": 500, "y": 500, "z": 0}}'

# 11. Execute Python in the editor
curl -s -X POST http://localhost:8523/exec/python \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"code": "print(len(unreal.EditorLevelLibrary.get_all_level_actors()))"}'

# 12. Create a new Blueprint asset
curl -s -X POST http://localhost:8523/scene/create-blueprint \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"parentClass": "Actor", "path": "/Game/Blueprints", "name": "BP_MyNewActor"}'
```
