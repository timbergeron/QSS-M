QSS-M reads mod titles from `mods/moddownloads.json` in the GitHub repository
selected by `cl_web_download_url` / `cl_web_download_url2` (q1tools by default).
The engine loads the last valid cache immediately and fetches updates in the
background, independently of opening the download menu.

For a new installed folder, add an entry to the manifest's `mods` array:

```json
{
  "install_dir": "newjam",
  "title": "New Jam"
}
```

An entry can describe a mod without providing a download. Existing downloadable
entries already supply these fields and require no changes. `title` is the mod
name used by the installed Mods menu and Discord presence; `description` remains
the download menu's detail text. Folder matching ignores case. With duplicate
folders, the last valid entry wins. Titles must fit in 127 bytes and contain no
control characters. Paths and traversal components are rejected.

A readable local `descript.ion` takes precedence, including an intentionally
blank first line. Otherwise the manifest title wins, followed by the engine's
existing content detection. Unknown folders retain the existing folder label.
Renamed folders can still be identified by the built-in content detection.

`modmetadata.cache` in the engine user directory stores the source URL and JSON.
Invalid or failed responses retain the last valid metadata and cache. Writes use
a temporary file and replacement; switching repositories clears the in-memory
metadata and accepts disk data only for the matching source URL. The cache holds
one source at a time. Dedicated servers do not start the background fetch.

Verify the metadata lookup and cache lifecycle with:

```
python3 Misc/stress/test_mod_metadata.py
```

An optional manifest filename also checks a downloaded copy of the live data.
