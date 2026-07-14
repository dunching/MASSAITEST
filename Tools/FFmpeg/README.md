# Project FFmpeg

This project vendors a Windows x64 FFmpeg command-line build so scripts can use a project-local binary without requiring a system-wide FFmpeg install.

## Binaries

- `Win64/bin/ffmpeg.exe`
- `Win64/bin/ffprobe.exe`

Use them from the repository root:

```powershell
.\Tools\FFmpeg\Win64\bin\ffmpeg.exe -version
.\Tools\FFmpeg\Win64\bin\ffprobe.exe -version
```

## Version

- Source: BtbN FFmpeg-Builds
- Package: `ffmpeg-master-latest-win64-lgpl.zip`
- Download URL: `https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-lgpl.zip`
- Installed version: `N-124532-gb4d11dffbf-20260518`
- Installed on: `2026-05-19`

## License

This build is distributed under the GNU Lesser General Public License. The bundled license text is in `Win64/LICENSE.txt`.

The executables are tracked through Git LFS via the repository `.gitattributes`.
