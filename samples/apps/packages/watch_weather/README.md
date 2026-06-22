# Watch Weather Example

A compact watch-weather source package used to validate package structure, local
resources, package BMP images, XHR data updates, system-state JS bindings,
event delegation, small-screen grid layout and Win32/pseudo-browser preview
parity.

The example is local-first, but it asks the host data service for
`/data/weather.json` through the JellyFrame `XMLHttpRequest` V0 subset. The
Win32 debug shell provides a mock response; hardware ports should provide their
own bounded data service. Remote pages are still not loaded by the renderer.
