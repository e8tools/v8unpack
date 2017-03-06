Name:           v8unpack
Version:        3.0.38
Release:        1%{?dist}
Summary:        Enterprise 8 unpack tool 

License:        Mozilla Public License 2.0
URL:            https://github.com/dmpas/v8unpack
Source0:        https://github.com/dmpas/v8unpack/archive/v.3.0.38.tar.gz

%if 0%{?rhel}
BuildRequires:  zlib-devel, boost-devel, zlib-devel-static, boost-devel-static, glibc-devel-static, libstdc++-devel-static
%endif
%if 0%{?fedora}||0%{?centos}
BuildRequires:  zlib-devel, boost-devel, zlib-static, boost-static, glibc-static, libstdc++-static
%endif

# workaround for rpm 4.13
%define _empty_manifest_terminate_build 0

%description
Enterprise 8 unpack tool

%prep
%setup -q -n v8unpack-v.%{version}


%build
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
%make_install


%files
%defattr(-,root,root)
/usr/bin/v8unpack
%doc



%changelog
* Mon Dec 26 2016 23:31:46 +0300 Sergey Batanov <sergey.batanov@dmpas.ru>
  - Added -build -nopack mode
  - Switched to static linking

* Tue Jul 21 2015 Shenja Sosna
- add rpm build 

* Sun Jun 28 2015 Sergey Batanov <sergey.batanov@dmpas.ru> 
- v8unpack (3.0.36-1) trusty; urgency=low
- Corrected installation binary path 

* Sun Jun 28 2015 Sergey Batanov <sergey.batanov@dmpas.ru> 
- Updated version number

* Sun Jun 28 2015 Sergey Batanov <sergey.batanov@dmpas.ru> 
- v8unpack (3.0.33-4) trusty; urgency=low
- Corrected build dependencies: zlib added 
- Sergey Batanov <sergey.batanov@dmpas.ru>  Sun, 28 Jun 2015 14:30:52 +0300

