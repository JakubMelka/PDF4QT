# Tool `forms` – Fill forms

The tool `forms` fills and resets form fields (Edit documents), like the user would: values are formatted by the format of the field. JavaScript of the document is not run, so calculated fields are not recomputed. One call is one undo step. Read the fields first by `document.get_form_fields`; background: pdf4qt://docs/forms.md.

| Action | Permission | Description |
| --- | --- | --- |
| `set_values` | Edit documents | Fill form fields (one undo step) |
| `reset` | Edit documents | Reset fields to their default values |

Hints of the tool: changes PDF4QT or documents, destructive.

## Common parameters

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `documentId` | string, at most 26 characters | no | Id of an open document (26 characters, from application.get_state). Omitted: the active document tab, else the only open document. |

## `forms.set_values`

Fill form fields (one undo step).

- Permission: Edit documents
- Common parameters: `documentId`
- Arguments: `{"action": "set_values", "set_values": {...}}`

### Parameters of `set_values`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `values` | array of 1–500 objects | yes | Values of the fields (one of value, checked, selected each) |
| `values[].name` | string, at most 1024 characters | yes | Fully qualified name, or a unique suffix of it |
| `values[].value` | string, at most 65536 characters | no | Text, combo box, radio group, single choice list: the value |
| `values[].checked` | boolean | no | Check box: checked or not |
| `values[].selected` | array of 0–1000 strings (at most 1024 characters) | no | Multiple choice list: the selected values |

### Result of `set_values`

`structuredContent` is `{"action": "set_values", "set_values": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `changed` | array of strings |  |
| `unchanged` | array of strings |  |
| `errors` | array of objects {name, message} |  |
| `note` | string |  |
| `undoDescription` | string | Description of the one undo step of the change in PDF4QT |

Each item has exactly one of `value`, `checked` or `selected`. Read-only fields and forms (locked documents, no permission to fill, XFA) are refused per field in `errors`.

### Examples of `set_values`

Text fields, a radio group and a check box:

```json
{"name": "forms", "arguments": {"action": "set_values", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "set_values": {"values": [{"name": "applicant.name", "value": "Jane Novak"}, {"name": "applicant.gender", "value": "female"}, {"name": "consent", "checked": true}]}}}
```

A multiple choice list and a field by the end of its name:

```json
{"name": "forms", "arguments": {"action": "set_values", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "set_values": {"values": [{"name": "interests", "selected": ["reading", "cycling"]}, {"name": "city", "value": "Prague"}]}}}
```

## `forms.reset`

Reset fields to their default values.

- Permission: Edit documents
- Common parameters: `documentId`
- Arguments: `{"action": "reset", "reset": {...}}`

### Parameters of `reset`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `names` | array of 1–500 strings (at most 1024 characters) | no | Fields to reset; omitted = all |

### Result of `reset`

`structuredContent` is `{"action": "reset", "reset": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `reset` | boolean | false = the fields had their default values already |
| `undoDescription` | string | Description of the one undo step of the change in PDF4QT |

A reset of fields, which have their default values already, changes nothing (`reset: false`).

### Examples of `reset`

Reset the whole form:

```json
{"name": "forms", "arguments": {"action": "reset", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE"}}
```

Reset two fields:

```json
{"name": "forms", "arguments": {"action": "reset", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "reset": {"names": ["applicant.name", "consent"]}}}
```
