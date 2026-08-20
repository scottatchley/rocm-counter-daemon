Name:		rocm-counter-daemon
Version:	0.1
Release:	1%{?dist}
Summary:	Simple C++ daemon to set up hardware device counters, wait for SIGUSR1, read the counters, write them, then exit.
License:	MIT
URL:		https://github.com/scottatchley/rocm-counter-daemon
Source0:	rocm-counter-daemon-0.1.tar.gz
BuildRequires:	cmake
BuildRequires:	gcc-c++

%define __requires_exclude ^lib(amd|roc).*\.so.*$

%description
Simple C++ daemon to set up hardware device counters, wait for SIGUSR1, read the counters, write them, then exit.

%prep
%autosetup -p1

%build
%cmake
%cmake_build

%install
%cmake_install
mkdir -p %{buildroot}/etc/rocm-counter-daemon
install -m 0644 config* %{buildroot}/etc/rocm-counter-daemon
mkdir -p %{buildroot}/etc/slurm/prolog.d
install -m 0755 rocm-counter-prologue.py %{buildroot}/etc/slurm/prolog.d/ 
mkdir -p %{buildroot}/etc/slurm/epilog.d
install -m 0755 rocm-counter-epilogue.py %{buildroot}/etc/slurm/epilog.d/
mkdir -p %{buildroot}/etc/slurm/lua.d/
install -m 0755 gpu-counter-opt.lua %{buildroot}/etc/slurm/lua.d/

%files
%license LICENSE
%{_bindir}/rocm-counter-daemon
/etc/rocm-counter-daemon/config*
/etc/slurm/prolog.d/rocm-counter-prologue.py
/etc/slurm/epilog.d/rocm-counter-epilogue.py
/etc/slurm/lua.d/gpu-counter-opt.lua

%changelog
* Thu Aug 20 2026 Matt Ezell <ezellma@ornl.gov> - 0.1-1
- Initial package release
