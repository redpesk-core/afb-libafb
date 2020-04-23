#---------------------------------------------
# spec file for package afb-libafb
#---------------------------------------------

Name:           afb-libafb
Version:        1.2
Release:        2%{?dist}
License:        GPLv3
Summary:        afb-libafb
Group:          Development/Libraries/C and C++
Url:            https://github.com/redpesk/afb-libafb
Source:         %{name}-%{version}.tar.gz

BuildRequires:  make
BuildRequires:  cmake
BuildRequires:  gcc-c++

BuildRequires:  pkgconfig(libmicrohttpd) >= 0.9.60
BuildRequires:  pkgconfig(libsystemd) >= 222
BuildRequires:  pkgconfig(json-c)
BuildRequires:  pkgconfig(afb-binding)
BuildRequires:  pkgconfig(cynagora)
BuildRequires:  file-devel

%description
Application Framework Binder core library

#---------------------------------------------
%package devel
Group:          Development/Libraries/C and C++
Requires:       %{name} = %{version}
Requires:       pkgconfig(afb-binding)
Provides:       pkgconfig(%{name}) = %{version}
Summary:        Development files for application Framework Binder core library

%description devel
Development files for application Framework Binder core library

#---------------------------------------------
%package static
Group:          Development/Libraries/C and C++
Requires:       %{name}-devel = %{version}
Provides:       pkgconfig(%{name}-static) = %{version}
Summary:        Development files for application Framework Binder static library

%description static
Development files for application Framework Binder static library

#---------------------------------------------
%prep
%setup -q -n %{name}-%{version}

%build
%cmake .
%__make %{?_smp_mflags}

%install
%make_install

%post

%postun

#---------------------------------------------
%files
%defattr(-,root,root)
%{_libdir}/libafb.so.1
%{_libdir}/libafb.so.1.2
%{_libdir}/libafbwsc.so.1
%{_libdir}/libafbwsc.so.1.2

#---------------------------------------------
%files devel
%defattr(-,root,root)
%dir %{_includedir}/libafb
%{_includedir}/libafb/*
%{_includedir}/afb/*
%{_libdir}/libafb.so
%{_libdir}/libafbwsc.so
%{_libdir}/pkgconfig/libafb.pc
%{_libdir}/pkgconfig/libafbwsc.pc

#---------------------------------------------
%files static
%defattr(-,root,root)
%{_libdir}/libafb-static.a
%{_libdir}/pkgconfig/libafb-static.pc

#---------------------------------------------
%changelog
* Wed Apr 22 2020 Jobol
- initial creation
