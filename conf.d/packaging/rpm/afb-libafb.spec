#
# spec file for package afb-libafb
#

Name:           afb-libafb
Version:        1.2
Release:        2%{?dist}
License:        GPLv3
Summary:        afb-libafb
Group:          Development/Libraries/C and C++
Url:            https://github.com/redpesk/afb-libafb
Source:         %{name}-%{version}.tar.gz
BuildRequires:  pkgconfig(libmicrohttpd) >= 0.9.60
BuildRequires:  make
BuildRequires:  cmake
BuildRequires:  pkgconfig(libsystemd) >= 222
BuildRequires:  pkgconfig(json-c)
BuildRequires:  pkgconfig(afb-binding)
BuildRequires:  pkgconfig(cynagora)
BuildRequires:  file-devel
BuildRequires:  gcc-c++

%if 0%{?suse_version}
Requires:       libmicrohttpd12 >= 0.9.60
%endif

%if 0%{?fedora_version}
Requires:       libmicrohttpd >= 0.9.60
%endif

%description
Application Framework Binder core library

%package devel
Group:          Development/Libraries/C and C++
Requires:       %{name} = %{version}
Provides:       pkgconfig(%{name}) = %{version}
Summary:        afb-libafb-devel

%description devel
Development files for application Framework Binder core library

%prep
%setup -q -n %{name}-%{version}

%build
%cmake .
%__make %{?_smp_mflags}

%install
%make_install

%post

%postun

%files
%defattr(-,root,root)
%{_libdir}/libafb.so.1
%{_libdir}/libafb.so.1.2
%{_libdir}/libafbwsc.so.1
%{_libdir}/libafbwsc.so.1.2

%files devel
%defattr(-,root,root)
%dir %{_includedir}/libafb
%{_includedir}/libafb/*
%{_includedir}/afb/*
%{_libdir}/libafb.so
%{_libdir}/libafbwsc.so
%{_libdir}/libafb.a
%{_libdir}/pkgconfig/*.pc

%changelog
* Wed Apr 22 2020 Jobol
- initial creation
