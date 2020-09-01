# Core library of Application Framework Binder

This project provides the library for building microservice architecture
binder like

This project is available here <https://github.com/redpesk/afb-libafb>.

## License and copying

This software if available in dual licensing. See file LICENSE.txt for detail

## dependencies

This project depends of two other project:

- afb-binding (LGPL-3.0):
  for definition of the interface of bindings
  <https://github.com/redpesk/afb-libafb>
- json-c (MIT):
  handling of json structures

It can use the other libraries:

- cynagora (Apache-2):
  for checking permissions
- libsystemd (LGPL-2.1+):
  management of events
- libmicrohttpd (LGPL-2.1+):
  for HTTP serveur and WebSocket negociation

## Building

You can build it using the following set of commands:

```sh
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j install
```

But the simplest way to build and install libafb is the use the
script mkbuild.sh as below:

```sh
./mkbuild.sh -p /usr/local install
```

## Deploy test code coverage

```sh
mkdir build
cd build
CMAKE_BUILD_TYPE=COVERAGE ../mkbuild.sh -f
make test
lcov --capture --directory . --exclude '/usr/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
xdg-open ./coverage_report/index.html
```
