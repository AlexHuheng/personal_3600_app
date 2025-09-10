all:
	make -C common
	make -C slip
	make -C thrift_slave
	make -C app

h3600_debug:
	make -C common
	make -C slip
	make -C thrift_slave
	make -C app H3600_DEBUG=TRUE

clean:
	make -C common clean
	make -C slip clean
	make -C thrift_slave clean
	make -C app clean


.PHONY: common slip thrift_slave app 
