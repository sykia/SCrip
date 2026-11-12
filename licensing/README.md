# Releases with verifiable licensing materials

Project code and original artwork: Copyright (c) 2026 sykia, GPL-3.0-only. Full text: LICENSE. Copyright is retained; the GPL permits commercial forks. Naming policy: TRADEMARKS.md.

Each extracted release package requires a companion `compliance.json` and source directory. Run:

```sh
python scripts/check_licenses.py --payload build/stage --manifest build/compliance.json
```

Manifest format: `files` maps every shipped file's relative path to an object containing `sha256` and `components` (a nonempty list of IDs). `components` maps each ID to `license`, `notice`, `source`, `source_sha256`. `notice` and `source` are paths relative to the manifest directory. `source` must be a corresponding source archive with patches, configuration and build instructions; an upstream link or binary package is insufficient. Archives are distributed alongside binaries. License texts and notices must also be included in the package itself. All files, including Java DEX, JAR and resources, must be accounted for. One DEX may contain multiple components.

Validation blocks unknown license expressions pending manual review, missing files, SHA-256 changes and missing sources/notices. It is not legal advice and does not prove the source archive corresponds to the binary; reproducible builds and review establish that. Do not assign GPL to every DLL without checking provenance.

Published-release CI expects `compliance.tar.gz`: `Windows`, `Linux-DEB`, `Linux-Arch`, `Android` directories with `payload/`, `compliance.json`, source archives and notices. Each platform manifest also contains `artifact_sha256`, the original release file's SHA-256. CI downloads actual EXE/APK/DEB/Arch packages, verifies that hash, extracts them and compares the complete inventory and hashes with the manifest instead of trusting the materials' payload copy. Manifests also account for installer support files, DEX contents and package metadata. Missing materials must cause validation to fail, rather than mark the release compliant.

## Single-platform releases

Release 1.4.1 contains only an Android APK. Corresponding application sources and the per-file manifest are in `licensing/releases/1.4.1/`, linked beside the APK in the release description. Unchanged dependency sources are available in the earlier public 1.4.0 `compliance.tar.gz`. `release.json` pins that archive's SHA-256 and explicitly lists platforms. The new manifest binds the materials to the new APK's SHA-256; the old application manifest is not reused.

CI verifies the base archive hash, restores materials using `scripts/prepare_release_materials.py`, substitutes new application sources and checks the actual APK with `scripts/audit_release.py --platform Android`. All other inventory, license, notice and source checks remain in place. Releases without such a descriptor still require materials for all four platforms.
