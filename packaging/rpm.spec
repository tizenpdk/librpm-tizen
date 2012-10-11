Name:           rpm
BuildRequires:  binutils
BuildRequires:  bzip2
BuildRequires:  libfile-devel
BuildRequires:  findutils
BuildRequires:  gcc
BuildRequires:  gettext-tools
BuildRequires:  glibc-devel
BuildRequires:  gzip
BuildRequires:  libacl-devel
BuildRequires:  bzip2-devel
BuildRequires:  libcap-devel
BuildRequires:  libelf-devel
BuildRequires:  libtool
BuildRequires:  lua-devel
BuildRequires:  make
BuildRequires:  pkgconfig(ncurses)
BuildRequires:  patch
BuildRequires:  popt-devel
BuildRequires:  xz-devel
BuildRequires:  zlib-devel
BuildRequires:  nss-devel
%if 0%{?enable_security}
BuildRequires:  uthash-devel
BuildRequires:  libxml2-devel
BuildRequires:  libattr-devel
BuildRequires:  libsmack-devel
%endif

Provides:       rpminst
Provides:	rpm-libs
Summary:        The RPM Package Manager
License:        GPL-2.0+
Group:          System/Packages
Version:        4.9.1.2
Release:        0
Source:         rpm-%{version}.tar.bz2
Source1:        RPM-HOWTO.tar.bz2
Source2:        RPM-Tips.html.tar.bz2
Source4:        rpm-tizen_macros
Source5:        rpmsort
Source6:        symset-table
Source8:        rpmconfigcheck
Source11:       db-4.8.30.tar.bz2
Source13:	find-docs.sh
Source22:	device-sec-policy
Patch2:         db.diff
# quilt patches start here
Patch11:        debugedit.diff
Patch12:        localetag.diff
Patch13:        missingok.diff
Patch14:        nameversioncompare.diff
Patch15:        dbfsync.diff
Patch16:        dbrointerruptable.diff
Patch17:        extcond.diff
Patch18:        refreshtestarch.diff
Patch19:        rpmrctests.diff
Patch20:        waitlock.diff
Patch21:        suspendlock.diff
Patch22:        weakdeps.diff
Patch23:        autodeps.diff
Patch24:        brp.diff
Patch25:        brpcompress.diff
Patch26:        checkfilesnoinfodir.diff
Patch27:        finddebuginfo.diff
Patch28:        findksyms.diff
Patch29:        findlang.diff
Patch30:        macrosin.diff
Patch31:        modalias.diff
Patch32:        platformin.diff
Patch33:        rpmpopt.diff
Patch34:        rpmrc.diff
Patch35:        taggedfileindex.diff
Patch36:        rpmqpack.diff
Patch37:        convertdb1static.diff
Patch38:        build.diff
Patch39:        modalias-kernel_module.diff
Patch40:        files.diff
Patch41:        debugedit-comp-dir.diff
Patch42:        perlprov.diff
Patch43:        rpm-shorten-changelog.diff
Patch44:        debugsource-package.diff
Patch45:        whatrequires-doc.diff
Patch46:        remove-brp-strips.diff
Patch47:        requires-ge-macro.diff
Patch48:        debugedit-canon-fix.diff
Patch49:        finddebuginfo-absolute-links.diff
Patch50:        firmware.diff
Patch51:        specfilemacro.diff
Patch52:        modalias-encode.diff
Patch53:        disttag-macro.diff
Patch54:        buildidprov.diff
Patch55:        debugsubpkg.diff
Patch56:        debuglink.diff
Patch57:        debuginfo-mono.patch
Patch58:        lazystatfs.diff
Patch59:        repackage-nomd5.diff
Patch60:        safeugid.diff
Patch61:        noprereqdeprec.diff
Patch62:        pythondeps.diff
Patch63:        fontprovides.diff
Patch64:        rpm-gst-provides.patch
Patch65:        langnoc.diff
Patch66:        initscriptsprov.diff
Patch67:        remove-translations.diff
Patch68:        no_rep_autop.diff
Patch69:        headeradddb.diff
Patch70:        dbprivate.diff
Patch71:        nobuildcolor.diff
Patch72:        fileattrs.diff
Patch73:        nomagiccheck.diff
Patch74:        findsupplements.diff
Patch75:        assumeexec.diff
Patch76:        buildpipe.diff
Patch77:        mono-find-requires.diff
Patch78:        debugedit-stabs-warning.diff
Patch79:        headerchk.diff
Patch80:        rpm-deptracking.patch
Patch81:        python3-abi-kind.diff
Patch82:        perl-python-attr.patch
Patch100:	security_4.9.1.patch
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
#
# avoid bootstrapping problem
%define _binary_payload w9.bzdio

%description
RPM Package Manager is the main tool for managing the software packages
of the SuSE Linux distribution.

RPM can be used to install and remove software packages. With rpm, it
is easy to update packages.  RPM keeps track of all these manipulations
in a central database.	This way it is possible to get an overview of
all installed packages.  RPM also supports database queries.

%package devel
Summary:        Include Files and Libraries mandatory for Development
Group:          System/Packages
Requires:       rpm = %{version}
# for people confusing the one with the other
#Recommends:     rpm-build = %{version}
Requires:       popt-devel

%description devel
This package contains the RPM C library and header files.  These
development files will simplify the process of writing programs which
manipulate RPM packages and databases and are intended to make it
easier to create graphical package managers or any other tools that
need an intimate knowledge of RPM packages in order to function.

%package build
Summary:        Tools and Scripts to create rpm packages
Group:          System/Packages
Requires:       rpm = %{version}
Provides:       rpmbuild rpm:%_bindir/rpmbuild
# SUSE's build essentials
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
Group: Development/Libraries
Requires: rpm = %{version}-%{release}
Requires: libsmack
Requires: libxml2
Requires: file
Requires: uthash
Requires: nss

%description security-plugin
This package contains the MSM security plugin for rpm that performs
security-related functionality. 


%prep
%setup -q -n rpm-%{version}
rm -rf sqlite
rm -rf beecrypt
#tar xjf %{SOURCE10}
tar xjf %{SOURCE11}
ln -s db-4.8.30 db
#ln -s beecrypt-4.1.2 beecrypt
chmod -R u+w db/*
#tar xjf %{SOURCE12}
#ln -s neon-0.24.7 neon
# will get linked from db3
rm -f rpmdb/db.h
%patch -P 2
%patch -P 11 -P 12 -P 13 -P 14 -P 15 -P 16 -P 17 -P 18 -P 19
%patch -P 20 -P 21 -P 22 -P 23 -P 24 -P 25 -P 26 -P 27 -P 28 -P 29
%patch -P 30 -P 31 -P 32 -P 33 -P 34 -P 35 -P 36 -P 37 -P 38 -P 39
%patch -P 40 -P 41 -P 42 -P 43 -P 44 -P 45 -P 46 -P 47 -P 48 -P 49
%patch -P 50 -P 51 -P 52 -P 53 -P 54 -P 55 -P 56 -P 57 -P 58 -P 59
%patch -P 60 -P 61 -P 62 -P 63 -P 64 -P 65 -P 66 -P 67 -P 68 -P 69
%patch -P 70 -P 71 -P 72 -P 73 -P 74 -P 75 -P 76 -P 77 -P 78 -P 79 
%patch -P 80 -P 81 -P 82
%patch -p1 -P 100
#chmod 755 scripts/find-supplements{,.ksyms}
#chmod 755 scripts/find-provides.ksyms scripts/find-requires.ksyms
#chmod 755 scripts/firmware.prov
#chmod 755 scripts/debuginfo.prov
tar -xjvf %{SOURCE1}
tar -xjvf %{SOURCE2}
if [ -s /etc/rpm/tizen_macros ]; then
	cp -a /etc/rpm/tizen_macros %{SOURCE4}
fi
cp -a %{SOURCE4} tizen_macros
rm -f m4/libtool.m4
rm -f m4/lt*.m4

%build
CPPFLAGS="$CPPFLAGS `pkg-config --cflags nss`"
export CPPFLAGS 
export CFLAGS="%{optflags} -ffunction-sections"
export LDFLAGS="-Wl,-Bsymbolic-functions -ffunction-sections"
%ifarch armv5tel
export CFLAGS="-g -O0 -fno-strict-aliasing -ffunction-sections"
%endif

%if %{_target_cpu}
%ifarch %arm
BUILDTARGET="--build=%{_target_cpu}-tizen-linux-gnueabi"
%else
BUILDTARGET="--build=%{_target_cpu}-tizen-linux"
%endif
%endif

autoreconf -i -f
./configure --disable-dependency-tracking --prefix=%{_prefix} --mandir=%{_mandir} --infodir=%{_infodir} \
--libdir=%{_libdir} --sysconfdir=/etc --localstatedir=/var  --with-lua \
--with-acl --with-cap  --enable-shared %{?with_python: --enable-python} $BUILDTARGET

rm po/de.gmo
make %{?_smp_mflags}
make convertdb1

%install
mkdir -p %{buildroot}/usr/lib
mkdir -p %{buildroot}/usr/share/locale
ln -s ../share/locale %{buildroot}/usr/lib/locale
%make_install
install -m 755 convertdb1 %{buildroot}/usr/lib/rpm
install -m 644 db3/db.h %{buildroot}/usr/include/rpm
# remove .la file and the static variant of libpopt
# have to remove the dependency from other .la files as well
#for f in %{buildroot}/%{_libdir}/*.la; do
#    sed -i -e "s,/%_lib/libpopt.la,-lpopt,g" $f
#done
mkdir -p %{buildroot}%{_sysconfdir}/rpm
cp -a tizen_macros %{buildroot}/usr/lib/rpm
mkdir -p %{buildroot}/usr/lib/rpm/tizen
install -m 755 %{SOURCE13} %{buildroot}/usr/lib/rpm/tizen
%if 0%{?enable_security}
install -m 644 %{SOURCE22} ${RPM_BUILD_ROOT}%{_sysconfdir}/device-sec-policy
%endif
ln -s ../tizen_macros %{buildroot}/usr/lib/rpm/tizen/macros
for d in BUILD RPMS SOURCES SPECS SRPMS BUILDROOT ; do
  mkdir -p %{buildroot}/usr/src/packages/$d
  chmod 755 %{buildroot}/usr/src/packages/$d
done
for d in %{buildroot}/usr/lib/rpm/platform/*-linux/macros ; do
  dd=${d%%-linux/macros}
  dd=${dd##*/}
  mkdir %{buildroot}/usr/src/packages/RPMS/$dd
  chmod 755 %{buildroot}/usr/src/packages/RPMS/$dd
done
mkdir -p %{buildroot}/var/lib/rpm
gzip -9 %{buildroot}/%{_mandir}/man[18]/*.[18]
export RPM_BUILD_ROOT
chmod 755 doc/manual
rm -rf doc/manual/Makefile*
rm -f %{buildroot}/usr/lib/rpmpopt
rm -rf %{buildroot}%{_mandir}/{fr,ja,ko,pl,ru,sk}
rm -f %{buildroot}%{_prefix}/share/locale/de/LC_MESSAGES/rpm.mo
rm -f %{buildroot}/usr/lib/rpm/cpanflute %{buildroot}/usr/lib/rpm/cpanflute2
install -m 755 %{SOURCE5} %{buildroot}/usr/lib/rpm
install -m 755 %{SOURCE6} %{buildroot}/usr/lib/rpm
install -m 755 scripts/find-supplements{,.ksyms} %{buildroot}/usr/lib/rpm
install -m 755 scripts/firmware.prov %{buildroot}/usr/lib/rpm
install -m 755 scripts/debuginfo.prov %{buildroot}/usr/lib/rpm
rm -f %{buildroot}/usr/lib/locale %{buildroot}/usr/lib/rpmrc
mkdir -p %{buildroot}/etc/rpm
chmod 755 %{buildroot}/etc/rpm
# remove some nonsense or non-working scripts
pushd %{buildroot}/usr/lib/rpm/
for f in rpm2cpio.sh rpm.daily rpmdiff* rpm.log rpm.xinetd freshen.sh u_pkg.sh \
         magic magic.mgc magic.mime* rpmfile *.pl javadeps brp-redhat \
         brp-strip-static-archive vpkg-provides*.sh http.req sql.req tcl.req \
         rpmdb_* brp-sparc64-linux brp-strip-comment-note brp-java-gcjcompile
do
    rm -f $f
done
for i in /usr/share/automake-*/*; do
  if test -f "$i" && test -f "${i##*/}"; then
    rm -f "${i##*/}"
  fi
done
popd
gzip -9 CHANGES
rm -rf %{buildroot}/%{_libdir}/python%{py_ver}
rm -f %{buildroot}%{_libdir}/*.la
rm -f %{buildroot}%{_libdir}/rpm-plugins/*.la
sh %{buildroot}/usr/lib/rpm/find-lang.sh %{buildroot} rpm
%ifarch armv7hl
# rpm is using the host_cpu as default for the platform, but armv7hl is not known by the kernel.
# so we need to enforce the platform here.
# We don't want to use armv7l because it would make us incompatible to Fedora and MeeGo plattforms.
echo -n armv7hl-suse-linux> %{buildroot}/etc/rpm/platform
%endif

%post
test -f var/lib/rpm/Packages || rpm --initdb
rm -f var/lib/rpm/Filemd5s var/lib/rpm/Filedigests var/lib/rpm/Requireversion var/lib/rpm/Provideversion


%files -f rpm.lang
%defattr(-,root,root)
%doc 	CHANGES.gz COPYING GROUPS
%doc 	doc/manual
%doc    RPM-HOWTO RPM-Tips
	/etc/rpm
	/bin/rpm
	/usr/bin/*
        %exclude /usr/bin/rpmbuild
	/usr/lib/rpm
%dir 	%{_libdir}/rpm-plugins
	%{_libdir}/rpm-plugins/exec.so
	%{_libdir}/rpm-plugins/sepolicy.so
	%{_libdir}/librpm.so.*
	%{_libdir}/librpmbuild.so.*
	%{_libdir}/librpmio.so.*
	%{_libdir}/librpmsign.so.*
%doc	%{_mandir}/man[18]/*.[18]*
%dir 	/var/lib/rpm
%dir 	%attr(755,root,root) /usr/src/packages/BUILD
%dir 	%attr(755,root,root) /usr/src/packages/SPECS
%dir 	%attr(755,root,root) /usr/src/packages/SOURCES
%dir 	%attr(755,root,root) /usr/src/packages/SRPMS
%dir	%attr(755,root,root) /usr/src/packages/RPMS
%dir	%attr(755,root,root) /usr/src/packages/BUILDROOT
%dir	%attr(755,root,root) /usr/src/packages/RPMS/*

%files build
%defattr(-,root,root)
/usr/bin/rpmbuild

%files devel
%defattr(644,root,root,755)
	/usr/include/rpm
        %{_libdir}/librpm.so
        %{_libdir}/librpmbuild.so
        %{_libdir}/librpmio.so
        %{_libdir}/librpmsign.so
        %{_libdir}/pkgconfig/rpm.pc
%if 0%{?enable_security}
%files security-plugin
%defattr(-,root,root)
%{_libdir}/rpm-plugins/msm.so
%config(noreplace) %{_sysconfdir}/device-sec-policy
%endif
