#!/bin/sh

set -x
yum -y install rpmdevtools &&
rpmdev-setuptree &&
rpm -Uvh $RPM_BASE_SRPM &&
cp $RPM_PATCH $HOME/rpmbuild/SOURCES/ &&
rpmbuild -bs $RPM_SPEC &&
mkdir -p $RPM_RESULTS &&
cp $HOME/rpmbuild/SRPMS/* $RPM_RESULTS/
