ARG pkg_mgr=apt
ARG ubuntu_version=22.04
ARG cyclus_tag=latest

FROM ghcr.io/cyclus/cyclus_${ubuntu_version}_${pkg_mgr}/cyclus:${cyclus_tag} AS areal
ARG make_cores=2

COPY . /areal
WORKDIR /areal

RUN python3 install.py -j ${make_cores} 

FROM areal AS deb-generation
WORKDIR /areal/build
RUN make package

FROM scratch AS deb-package
COPY --from=deb-generation /areal/build/areal*.deb /

FROM areal AS areal-test
RUN areal_unit_tests