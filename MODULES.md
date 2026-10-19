# Modules / Модули

A module is a folder (or a `.hsmod` = tar.gz of it) that changes how HomeServer OS looks or adds a component.
Модуль — папка (или `.hsmod`, архив tar.gz), которая меняет внешний вид HomeServer OS или добавляет компонент.

```
mymodule/
  module.conf      NAME=mymodule  VERSION=1.0  TYPE=theme|component|service  DESC="..."  AUTHOR="..."
  theme/dialogrc   colours of all menus            (optional)
  theme/banner     text shown at boot              (optional)
  files/           copied into / on enable         (optional, removed again on disable)
  menu.entry       lines "Title|command"  -> menu "Modules"   (optional)
  start / stop     scripts run at boot / on disable           (optional)
  enable.sh, disable.sh   extra hooks                         (optional)
```

Commands / Команды: `hs-mod list | info N | install FOLDER|FILE.hsmod|URL | enable N | disable N | remove N | new N | pack N`.
Menu: **Modules** (key 6). `hs-mod new mytheme` makes a ready template, `hs-mod pack mytheme` makes a `mytheme.hsmod` you can share.
Built-in modules: themes `retro-green`, `amber`, `neon`, `matrix`; components `quick-share` (share a folder over HTTP :8081), `backup` (tar.gz of settings and home folders), `netcheck` (connection and speed test).
Security: a module runs as root — install only modules you trust. / Модуль выполняется от root — ставьте только доверенные.
