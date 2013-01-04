Name:           python-rpm
Version:        4.10.91
Release:        0
Summary:        Python Bindings for Manipulating RPM Packages
License:        GPL-2.0+
Group:          System/Packages
Source99:       rpm.spec
BuildRequires:  gettext-tools
BuildRequires:  file-devel
BuildRequires:  libacl-devel
BuildRequires:  bzip2-devel
BuildRequires:  libcap-devel
BuildRequires:  libelf-devel
BuildRequires:  libtool
BuildRequires:  lua-devel
BuildRequires:  ncurses-devel
BuildRequires:  popt-devel
BuildRequires:  python-devel
BuildRequires:  xz-devel
BuildRequires:  zlib-devel
BuildRequires:  nss-devel
Requires:       rpm = %{version}
%{expand:%(sed -n -e '/^Source0:/,/^BuildRoot:/p' <%_sourcedir/rpm.spec)}
%global with_python 2

%description
The python-rpm package contains a module that permits applications
written in the Python programming language to use the interface
supplied by RPM Package Manager libraries.

This package should be installed if you want to develop Python programs
that will manipulate RPM packages and databases.

%prep
%{expand:%(sed -n -e '/^%%prep/,/^%%install/p' <%_sourcedir/rpm.spec | sed -e '1d' -e '$d')}

%install
mkdir -p %{buildroot}%{_prefix}/lib
# only installing in python/ does not work because rpm links against
# installed libs at install time
%make_install
find %{buildroot} -not -type d -and -not -path %{buildroot}%{_libdir}/python*/site-packages/rpm/\* -print0 | xargs -0 rm
pushd %{buildroot}/%{_libdir}/python*/site-packages/rpm
rm -f _rpmmodule.a _rpmmodule.la
#python %{_libdir}/python*/py_compile.py *.py
#python -O %{_libdir}/python*/py_compile.py *.py
popd

%files
%defattr(-,root,root)
%{_libdir}/python*/*/*

%changelog
