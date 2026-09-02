# Build VitaSearch on GitHub

1. Upload the **contents** of this `vitasearch` folder to the root of your GitHub repository.
2. Make sure `.github/workflows/vita-build.yml` is present in GitHub.
3. Open **Actions**.
4. Select **Build VitaSearch**.
5. Press **Run workflow** and choose `main` (or `master`).
6. When the job is green, open the run and download the **VitaSearch-VPK** artifact.
7. Inside the artifact is `VitaSearch.vpk`.

The workflow also runs automatically when files under `vita/` are pushed.
