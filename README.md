monitorctl: DDC monitor controls for the OSX command line
(considerably reduced fork of [ddcctl](https://github.com/kfix/ddcctl) to only work with dual monitors which are identical.
----
Adjust your external monitors' built-in controls from the OSX shell:
* brightness
* contrast

And *possibly* (if your monitor firmware is well implemented):
* input source
* built-in speaker volume
* on/off/standby
* rgb colors
* color presets
* reset

Install
----
```bash
make install
```

For an On-Screen Display use [OSDisplay.app](https://github.com/zulu-entertainment/OSDisplay)

Usage
----
Run `monitorctl -c VALUE -b VALUE` to set contrast and brightness to both monitors simultaneously.
