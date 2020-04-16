# Core library of Application Framework Binder

This project provides the library for building microservice architecture
binder like

This project is available here https://github.com/redpesk/afb-libafb.

## License and copying

This software if available in dual licensing. See file LICENSE.txt for detail

## dependencies

This project depends of the other project: afb-binding.

Currently it also depends at least of json-c.

It can use the other libraries:

- libmicrohttpd (LGPL-2.1+):
  for HTTP serveur and WebSocket negociation
- cynagora (Apache-2):
  for checking permissions
- libsystemd (LGPL-2.1+):
  management of events

## Building

You can build it using the following set of commands:

        mkdir build
        cd build
        cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
        make -j install

But the simplest way to build and install libafb is the use the
script mkbuild.sh as below:

        ./mkbuild.sh -p /usr/local install
