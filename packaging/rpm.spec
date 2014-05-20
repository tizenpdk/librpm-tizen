%define rpmlibdir %{_prefix}/lib
%define rpmhome %{rpmlibdir}/rpm

Name:           rpm
Summary:        The Package Manager
License:        GPL-2.0+
Group:          Base/Package Management
Version:        4.11.0.1
Release:        0
Url:            http://www.rpm.org
BuildRequires:  binutils
BuildRequires:  bzip2
BuildRequires:  file-devel
BuildRequires:  findutils
BuildRequires:  gcc
BuildRequires:  gettext-tools
BuildRequires:  glibc-devel
BuildRequires:  gzip
BuildRequires:  libacl-devel
BuildRequires:  libattr-devel
BuildRequires:  pkgconfig(bzip2)
BuildRequires:  pkgconfig(libcap)
BuildRequires:  libelf-devel
BuildRequires:  libtool
BuildRequires:  pkgconfig(lua)
BuildRequires:  make
BuildRequires:  pkgconfig(ncurses)
BuildRequires:  patch
BuildRequires:  pkgconfig(popt)
BuildRequires:  xz-devel
BuildRequires:  pkgconfig(zlib)
BuildRequires:  pkgconfig(nss)
BuildRequires:  uthash-devel
BuildRequires:  pkgconfig(libxml-2.0)
BuildRequires:  pkgconfig(libsmack)
BuildRequires:  fdupes

Provides:       rpminst
Provides:       rpm-libs

Source1:       db-4.8.30.tar.bz2
Source2:       db-4.8.30-integration.dif
Source4:       rpm-tizen_macros
Source8:       rpmconfigcheck
Source13:      find-docs.sh
Source22:      device-sec-policy
Source23:      find-provides.ksyms
Source1001:    rpm.manifest
Source0:        rpm-%{version}.tar.bz2
#
# avoid bootstrapping problem
%define _binary_payload w9.bzdio

%description
RPM Package Manager is the main tool for managing the software packages
of Tizen.

RPM can be used to install and remove software packages. With rpm, it
is easy to update packages.  RPM keeps track of all these manipulations
in a central database.	This way it is possible to get an overview of
all installed packages.  RPM also supports database queries.

%package devel
Summary:        Include Files and Libraries mandatory for Development
Requires:       rpm = %{version}
Requires:       pkgconfig(popt)

%description devel
This package contains the RPM C library and header files.  These
development files will simplify the process of writing programs which
manipulate RPM packages and databases and are intended to make it
easier to create graphical package managers or any other tools that
need an intimate knowledge of RPM packages in order to function.

%package build
Summary:        Tools and Scripts to create rpm packages
Requires:       rpm = %{version}
Provides:       rpmbuild rpm:%{_bindir}/rpmbuild
Requires:       bzip2
Requires:       xz
Requires:       gzip
Requires:       binutils
Requires:       make
Requires:       gcc
Requires:       findutils
Requires:       patch
Requires:       glibc-devel

%description build
If you want to build a rpm, you need this package. It provides rpmbuild
and requires some packages that are usually required

%package security-plugin
Summary: MSM security plugin for rpm
Requires: rpm = %{version}-%{release}
Requires: smack
Requires: nss

%description security-plugin
This package contains the MSM security plugin for rpm that performs
security-related functionality.

%package -n python-rpm
Summary: Python Bindings for Manipulating RPM Packages
Requires:       rpm = %{version}
BuildRequires:  pkgconfig(python)

%description -n python-rpm
The python-rpm package contains a module that permits applications
written in the Python programming language to use the interface
supplied by RPM Package Manager libraries.

This package should be installed if you want to develop Python programs
that will manipulate RPM packages and databases.

%prep
%setup -q -n rpm-%{version}
cp %{SOURCE1001} .
rm -rf sqlite
tar xjf %{S:1}
ln -s db-4.8.30 db
chmod -R u+w db/*
# will get linked from db3
rm -f rpmdb/db.h
patch -p0 < %{S:2}
if [ -s %{_sysconfdir}/rpm/tizen_macros ]; then
    cp -a %{_sysconfdir}/rpm/tizen_macros %{SOURCE4}
fi
cp -a %{SOURCE4} tizen_macros
rm -f m4/libtool.m4
rm -f m4/lt*.m4

%build
CPPFLAGS="$CPPFLAGS `pkg-config --cflags nss`"
export CPPFLAGS
export CFLAGS="%{optflags} -ffunction-sections"
export LDFLAGS="${LDFLAGS} -Wl,-Bsymbolic-functions -ffunction-sections"
%ifarch armv5tel
export CFLAGS="-g -O0 -fno-strict-aliasing -ffunction-sections"
%endif

%reconfigure \
    --disable-dependency-tracking \
    --with-lua \
    --with-acl \
    --with-cap \
    --enable-shared \
    --enable-python \
    --with-msm 

make %{?_smp_mflags}

%install
mkdir -p %{buildroot}%{rpmlibdir}
mkdir -p %{buildroot}%{_datadir}/locale
ln -s ../share/locale %{buildroot}%{rpmlibdir}/locale
%make_install
install -m 644 db3/db.h %{buildroot}%{_includedir}/rpm
mkdir -p %{buildroot}%{_sysconfdir}/rpm
cp -a tizen_macros %{buildroot}%{rpmhome}
mkdir -p %{buildroot}%{rpmhome}/tizen
install -m 755 %{SOURCE13} %{buildroot}%{rpmhome}/tizen
install -m 755 %{SOURCE23} %{buildroot}%{rpmhome}
install -m 644 %{SOURCE22} %{buildroot}%{_sysconfdir}/device-sec-policy
install -m 644 %{SOURCE22} %{buildroot}%{__plugindir}/msm-device-sec-policy
ln -s ../tizen_macros %{buildroot}%{rpmhome}/tizen/macros
for d in BUILD RPMS SOURCES SPECS SRPMS BUILDROOT ; do
  mkdir -p %{buildroot}%{_usrsrc}/packages/$d
  chmod 755 %{buildroot}%{_usrsrc}/packages/$d
done
for d in %{buildroot}%{rpmhome}/platform/*-linux/macros ; do
  dd=${d%%-linux/macros}
  dd=${dd##*/}
  mkdir -p %{buildroot}%{_usrsrc}/packages/RPMS/$dd
  chmod 755 %{buildroot}%{_usrsrc}/packages/RPMS/$dd
done
mkdir -p %{buildroot}%{_localstatedir}/lib/rpm
gzip -9 %{buildroot}%{_mandir}/man[18]/*.[18]
export RPM_BUILD_ROOT
chmod 755 doc/manual
rm -rf doc/manual/Makefile*
rm -f %{buildroot}%{rpmlibdir}/rpmpopt
rm -rf %{buildroot}%{_mandir}/{fr,ja,ko,pl,ru,sk}
rm -f %{buildroot}%{_datadir}/locale/de/LC_MESSAGES/rpm.mo
rm -f %{buildroot}%{rpmhome}/cpanflute %{buildroot}%{rpmhome}/cpanflute2
install -m 755 scripts/find-supplements{,.ksyms} %{buildroot}%{rpmhome}
install -m 755 scripts/firmware.prov %{buildroot}%{rpmhome}
install -m 755 scripts/debuginfo.prov %{buildroot}%{rpmhome}
rm -f %{buildroot}%{rpmlibdir}/locale %{buildroot}%{rpmlibdir}/rpmrc
mkdir -p %{buildroot}%{_sysconfdir}/rpm
chmod 755 %{buildroot}%{_sysconfdir}/rpm
mkdir -p %{buildroot}%{rpmhome}/macros.d
# remove some nonsense or non-working scripts
pushd %{buildroot}%{rpmhome}/
for f in rpm2cpio.sh rpm.daily rpmdiff* rpm.log rpm.xinetd freshen.sh u_pkg.sh \
         magic magic.mgc magic.mime* rpmfile *.pl javadeps brp-redhat \
         brp-strip-static-archive vpkg-provides*.sh http.req sql.req tcl.req \
         rpmdb_* brp-sparc64-linux brp-strip-comment-note brp-java-gcjcompile
do
    rm -f $f
done
for i in %{_datadir}/automake-*/*; do
  if test -f "$i" && test -f "${i##*/}"; then
    rm -f "${i##*/}"
  fi
done
popd
rm -f %{buildroot}%{_libdir}/*.la
rm -f %{buildroot}%{__plugindir}/*.la

%fdupes %{buildroot}%{rpmhome}/platform

sh %{buildroot}%{rpmhome}/find-lang.sh %{buildroot} rpm

%ifarch armv7hl armv7l
# rpm is using the host_cpu as default for the platform,
#but armv7hl is not known by the kernel.
# so we need to enforce the platform here.
echo -n %{_target_cpu}-tizen-linux-gnueabi > %{buildroot}%{_sysconfdir}/rpm/platform
%endif

%post
/sbin/ldconfig
test -f %{_dbpath}/Packages || rpm --initdb
rm -f %{_dbpath}/Filemd5s \
      %{_dbpath}/Filedigests \
      %{_dbpath}/Requireversion \
      %{_dbpath}/Provideversion

%postun
/sbin/ldconfig

%files
%manifest %{name}.manifest
%defattr(-,root,root)
%license COPYING
%{_sysconfdir}/rpm
/bin/rpm
%{_bindir}/rpm2cpio
%{_bindir}/rpmdb
%{_bindir}/rpmkeys
%{_bindir}/rpmquery
%{_bindir}/rpmverify
%{_bindir}/rpmqpack
%attr(0755, root, root) %dir %{rpmhome}
%{rpmhome}/macros
%{rpmhome}/macros.d
%{rpmhome}/rpmpopt*
%{rpmhome}/rpmrc
%{rpmhome}/tizen/macros
%{rpmhome}/tizen_macros
%{rpmhome}/rpm.supp
%{rpmhome}/tgpg
%{rpmhome}/platform
%dir    %{__plugindir}
%{__plugindir}/exec.so
%{_libdir}/librpm.so.*
%{_libdir}/librpmio.so.*
%{_libdir}/librpmbuild.so.*
%{_libdir}/librpmsign.so.*
%dir    %{_localstatedir}/lib/rpm
%dir    %attr(755,root,root) %{_usrsrc}/packages/BUILD
%dir    %attr(755,root,root) %{_usrsrc}/packages/SPECS
%dir    %attr(755,root,root) %{_usrsrc}/packages/SOURCES
%dir    %attr(755,root,root) %{_usrsrc}/packages/SRPMS
%dir    %attr(755,root,root) %{_usrsrc}/packages/RPMS
%dir    %attr(755,root,root) %{_usrsrc}/packages/BUILDROOT
%dir    %attr(755,root,root) %{_usrsrc}/packages/RPMS/*

%files build
%manifest %{name}.manifest
%defattr(-,root,root)
%{_bindir}/rpmbuild
%{_bindir}/gendiff
%{_bindir}/rpmspec
%{_bindir}/rpmsign
%{rpmhome}/tizen/find-*
%{rpmhome}/brp-*
%{rpmhome}/find-supplements*
%{rpmhome}/check-*
%{rpmhome}/debugedit
%{rpmhome}/find-debuginfo.sh
%{rpmhome}/find-lang.sh
%{rpmhome}/find-provides.ksyms
%{rpmhome}/*provides*
%{rpmhome}/*requires*
%{rpmhome}/*deps*
%{rpmhome}/*.prov
%{rpmhome}/*.req
%{rpmhome}/macros.*
%{rpmhome}/fileattrs

%files devel
%manifest %{name}.manifest
%defattr(644,root,root,755)
%attr(755,root,root) %{_bindir}/rpmgraph
%{_includedir}/rpm
%{_libdir}/librpm.so
%{_libdir}/librpmbuild.so
%{_libdir}/librpmio.so
%{_libdir}/librpmsign.so
%{_libdir}/pkgconfig/rpm.pc

%files security-plugin
%manifest %{name}.manifest
%defattr(-,root,root)
%{__plugindir}/msm.so
%{__plugindir}/msm-device-sec-policy
%config(noreplace) %{_sysconfdir}/device-sec-policy

%files -n python-rpm
%defattr(-,root,root)
%{_libdir}/python*/site-packages/rpm
%attr(755,root,root) %{_libdir}/python*/site-packages/rpm/transaction.py

%lang_package

%docs_package
%doc     GROUPS
