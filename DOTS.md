# Preparing a Dots Workspace

The source archive includes ORBIT and its local Git repository (`.git`). When this workspace note was originally written, no connection to Dots was available; a repository was not created or pushed inside Dots.

After preparing the source folder as your Dots working directory, use these commands to inspect the repository and continue development.

```bash
cd fm1-orbit
git status
git log -1 --oneline
python -m pip install -r requirements.txt
python tools/orbit_check.py
python tools/orbit_preview.py --port 8080
```

For a hardware installation package, configure the JieLi toolchain and AC79 SDK as described in [BUILDING.md](BUILDING.md). Check resource budgets after a successful target build, then perform FM-1 hardware tests. Exposing the web preview from Dots depends on that environment's port-sharing features; the server binds to localhost by default.

The original development goals included target builds and resource budgets, sampler editing integration and looping a selected Tape region. See [the validation record](docs/VALIDATION.md) for subsequent results. Bluetooth still requires separate A2DP/SBC and HAL integration.
