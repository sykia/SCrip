# Wiki maintenance

The public documentation lives at <https://github.com/sykia/sange/wiki>. Its Markdown sources are maintained in [`wiki/`](wiki/Home.md). `_Sidebar.md` and `_Footer.md` provide GitHub Wiki navigation. Extensionless links refer to wiki page names; repository references use absolute GitHub links.

## Updating the wiki

Verify changes against code, packaging and actual release assets. Distinguish current-source features from published binaries. Do not infer device support from desktop-only tests. Maintain documentation in English.

GitHub Wiki has a separate Git repository. It must already have a first page created through GitHub before cloning. From the project root, use a fresh checkout under the ignored build directory:

```sh
git clone https://github.com/sykia/sange.wiki.git build/wiki-publish
cp docs/wiki/*.md build/wiki-publish/
git -C build/wiki-publish diff --check
git -C build/wiki-publish diff
git -C build/wiki-publish add -- '*.md'
git -C build/wiki-publish commit -m "Update Sange wiki"
git -C build/wiki-publish push origin HEAD
```

If that checkout already exists, inspect its status and pull with `git -C build/wiki-publish pull --ff-only` before copying. Preserve unrelated pages and remote edits; review differences before publishing. Verify the rendered Home, navigation and changed pages after the push. Do not copy this maintenance file into the wiki.
