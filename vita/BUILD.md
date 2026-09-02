# Vita build notes

Install VitaSDK and make sure `$VITASDK` is set.

The project links against `vita2d`, `curl`, `png`, `jpeg` and `z`.
Depending on your VitaSDK installation you may need to install/build vita2d and curl ports first.

Build:

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

Output: `VitaSearch.vpk`

The client includes a lightweight controller-driven address/search keyboard, so arbitrary URLs and Google queries can be entered directly on the Vita.
