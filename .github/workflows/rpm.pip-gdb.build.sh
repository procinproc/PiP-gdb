#!/bin/sh

set -x

# installation error may be ok
yum -y install python-devel
yum -y install texinfo-tex texlive-ec texlive-cm-super

yum -y install rpmdevtools &&
rpmdev-setuptree &&
rpm -Uvh $RPM_SRPM &&
time rpmbuild --define "pip_major_version $PIP_MAJOR_VERSION" \
	-bb $HOME/rpmbuild/SPECS/$RPM_SPEC &&
mkdir -p $RPM_RESULTS &&
cp -R $HOME/rpmbuild/RPMS/* $RPM_RESULTS/
