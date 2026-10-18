# Enable the video player in the GitHub README

The README already contains a preview and a link to the complete video. A normal repository-relative MP4 link does not create GitHub's attachment video player. To play the video directly inside the README, GitHub needs to host it as a Markdown attachment.

1. Commit/push `README.md` and `docs/` with your project changes.
2. Open `README.md` on GitHub and click **Edit this file** (the pencil).
3. Drag **`docs/media/vanta-riot-highlights.mp4`** into the editor. Wait for the upload to finish.
4. GitHub inserts a URL beginning with **`https://github.com/user-attachments/assets/`**. Leave that URL on its own line, outside a code block. Replace the preview/link block between `GAMEPLAY_VIDEO_START` and `GAMEPLAY_VIDEO_END` with it.
5. Use **Preview** to check the video player, then commit the README change.

The upload file contains the entire highlight, including the original studio and raylib scenes, with its original audio. It is compressed to 720p to fit under GitHub's 10 MB attachment limit on free accounts. The original 1080p master remains at `bin/review/vanta-riot-highlights.mp4` (the `bin/` folder is ignored by Git).

If you already uploaded the attachment, you can apply its URL locally:

```sh
python tools/embed_readme_video.py "https://github.com/user-attachments/assets/YOUR-ACTUAL-ATTACHMENT-ID"
```

Then commit/push the updated README. The helper accepts only real UUID-shaped GitHub attachment URLs; it does not upload or publish anything.

GitHub's upload documentation: https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/attaching-files
