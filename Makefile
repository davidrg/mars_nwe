# Makefile mars_nwe: 31-Jan-96

VPATH=

all:    rmeflag mk.li config.h nw.ini
	@if [ -r .eflag ] ; then \
	echo ""; \
	echo "********************************************************"; \
	cat .eflag; rm -f .eflag; \
	echo "";\
	echo "Please make your changes and run make again"; \
	echo "********************************************************"; \
	echo "";\
	echo ""; else ./mk.li  && (\
	if [ -r .mk.notes ] ; then echo "" ; \
	echo ""; \
	echo "********************************************************" ; \
	echo ""; \
	cat .mk.notes; rm .mk.notes ; \
	echo ""; \
	echo "********************************************************" ; \
	echo "";  echo "" ; fi ) fi

install:
	./mk.li $@

install_ini: nw.ini
	./mk.li $@

clean:  mk.li nw.ini
	./mk.li $@

distrib: mk.li nw.ini
	./mk.li $@

mk.li:  examples/mk.li
	@if [ -r $@ ] ; then \
	cp -f $@ $@.org && ( \
	  echo "********************************************************"; \
	  echo "";\
	  echo "saved: $@ -> $@.org, there is a new examples/$@"; \
	  echo "";\
	  echo "********************************************************"; \
	  echo "" ) ; fi
	@ echo ""
	@ echo ""
	@ - cp -i examples/$@  .
	@ touch -c $@
	@ echo ""
	@ echo "********************************************************"
	@ echo ""
	@ echo "perhaps $@ is new and you need to edit it."
	@ echo ""
	@ echo "********************************************************"
	@ echo ""
	@ echo "" > .eflag

config.h: examples/config.h
	@if [ -r $@ ] ; then echo "note:examples/$@ is newer then $@" >> .eflag ;\
	echo "$@ will be touched now" >> .eflag; touch -c $@ ; \
	else cp examples/$@ . ; \
        echo "$@ created (from examples/$@) Please edit $@" >> .eflag;\
        echo "and change it to your requirements." >> .eflag ; fi

rmeflag:
	@- rm -f .eflag

nw.ini: examples/nw.ini
	@if [ -r $@ ] ; then echo "NOTE:examples/$@ is newer then $@" > .mk.notes ; \
	echo "please compare examples/$@ with $@" >> .mk.notes; \
	echo "make the changes you need and touch $@" >> .mk.notes; \
	else  cp examples/$@ . ; \
        echo "$@ created (from examples/$@) Please edit $@" > .mk.notes;\
        echo "and change it to your requirements." >> .mk.notes ; fi
