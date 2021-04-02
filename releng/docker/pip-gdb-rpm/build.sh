#!/bin/sh

set -x
yum -y install rpmdevtools &&
rpmdev-setuptree &&
rpm -Uvh $RPM_SRPM &&
time rpmbuild -bb $HOME/rpmbuild/SPECS/$RPM_SPEC &&
mkdir -p $RPM_RESULTS &&
cp -R $HOME/rpmbuild/RPMS/* $RPM_RESULTS/
