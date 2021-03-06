# Use custom autoreq script for filtering out all librpm* stuff
# (basically everything that the package would auto-provide) in order to make
# the package installable
%define _use_internal_dependency_generator 0
%define __find_requires bash -c "/usr/lib/rpm/find-requires | grep -v librpm"

%define rpmlibdir %{_prefix}/lib
%define rpmhome %{rpmlibdir}/rpm

Name:           librpm-tizen
Summary:        The RPM libraries for git-buildpackage
License:        GPL-2.0+
Group:          Development/Tools/Building
Version:        4.11.0.1.tizen20170704
Release:        0
Url:            http://www.rpm.org
BuildRequires:  binutils
BuildRequires:  bzip2
BuildRequires:  file-devel
BuildRequires:  findutils
BuildRequires:  gcc
BuildRequires:  glibc-devel
BuildRequires:  gzip
BuildRequires:  libtool
%if 0%{?suse_version}
BuildRequires:  pkgconfig(lua)
%else
BuildRequires:  lua-devel
%endif
BuildRequires:  make
BuildRequires:  patch
%if 0%{?fedora} || 0%{?centos_ver} || 0%{?centos_version}
BuildRequires:  popt-devel
%else
BuildRequires:  pkgconfig(popt)
%endif
BuildRequires:  pkgconfig(zlib)
BuildRequires:  pkgconfig(nss)
BuildRequires:  pkg-config
%if 0%{?centos_ver} || 0%{?centos_version}
BuildRequires:  python-devel
%else
BuildRequires:  pkgconfig(python) >= 2.6
%endif
# Disable security
%if 0
BuildRequires:  uthash-devel
BuildRequires:  pkgconfig(libxml-2.0)
BuildRequires:  pkgconfig(libsmack)
%endif
%if 0%{?suse_version}
BuildRequires:  fdupes
%endif
%if 0%{?fedora} || 0%{?centos_ver} >= 7
BuildRequires:  libdb-devel
%else
BuildRequires:  db-devel
%endif

Source4:       rpm-tizen_macros
Source8:       rpmconfigcheck
Source13:      find-docs.sh
Source22:      device-sec-policy
Source23:      find-provides.ksyms
Source1001:    rpm.manifest
Source0:        rpm-%{version}.tar.gz
AutoProv:       No

Provides:       rpm-tizen = %{version}-tizen20140723
#
# avoid bootstrapping problem
%define _binary_payload w9.bzdio
#
# Python module name
%define python_mod_name rpm_tizen

%description
RPM Package Manager is the main tool for managing the software packages
of Tizen.

RPM can be used to install and remove software packages. With rpm, it
is easy to update packages.  RPM keeps track of all these manipulations
in a central database.	This way it is possible to get an overview of
all installed packages.  RPM also supports database queries.

This is a special stripped-down version of RPM, only intended to be used by the
git-buildpackage tool. This package doesn't interfere with the RPM libraries of
the host system and it only contains rpmlib and rpm-python.

%prep
%setup -q -n rpm-%{version}
cp %{SOURCE1001} .
rm -rf sqlite
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

autoreconf -i -f
%configure \
    --libdir=%{_libdir}/%{name} \
    --disable-dependency-tracking \
    --with-lua \
    --without-acl \
    --without-cap \
    --enable-shared \
    --enable-python \
    --without-msm \
    --with-external-db \
    PYTHON_MODULENAME=%{python_mod_name}

make %{?_smp_mflags}

%install
# Install into a temporary location
make install DESTDIR="`pwd`/tmp_install"

# And only copy the files that we want
install -d %{buildroot}%{_libdir}/%{name}
install -d %{buildroot}%{python_sitearch}
cp -ax tmp_install/%{_libdir}/%{name}  %{buildroot}%{_libdir}/
cp -ax tmp_install/%{python_sitearch}/%{python_mod_name} %{buildroot}%{python_sitearch}/

# Install extra sources
install -m644 %{SOURCE4} %{buildroot}%{_libdir}/%{name}/rpm/tizen_macros
install -d %{buildroot}%{_libdir}/%{name}/rpm/tizen
ln -s ../tizen_macros %{buildroot}%{_libdir}/%{name}/rpm/tizen/macros

# Delete unwanted development files etc.
find %{buildroot} -name "*.la" | xargs rm -f --
find %{buildroot}/%{_libdir}/%{name} -name "*.so" | xargs rm -f --
rm -rf "%{buildroot}%{_libdir}/%{name}/pkgconfig"
rm -rf "%{buildroot}%{_libdir}/%{name}/rpm-plugins"

# Compile python modules (Fedora does this automatically) and find duplicates
%if 0%{?suse_version}
%py_compile %{buildroot}/%{python_sitearch}/%{python_mod_name}/
%py_compile -O %{buildroot}/%{python_sitearch}/%{python_mod_name}/

%fdupes %{buildroot}/%{python_sitearch}/
%endif

%files
%defattr(-,root,root,-)
%dir %{_libdir}/%{name}
%dir %{python_sitearch}/%{python_mod_name}/
%{_libdir}/%{name}/*.so.*
%{_libdir}/%{name}/rpm
%{python_sitearch}/%{python_mod_name}/*
