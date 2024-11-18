ifdef ASAN
LIBS += -lasan
endif

all:
	$(CC) -g hsi2s_qmi_test.c -lhsi2sqmi -o hsi2s_qmi_test $(LIBS)

clean:
	rm -f hsi2s_qmi_test

install:
	mkdir -p $(DESTDIR)$(bindir)
	install hsi2s_qmi_test $(DESTDIR)$(bindir)
