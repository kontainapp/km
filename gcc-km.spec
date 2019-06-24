%rename link old_link

*lib:
-lruntime -lkmpthread

*libgcc:
-lgcc -lgcc_eh

*startfile:
crtbeginT.o%s

*endfile:
crtend.o%s

*link:
%(old_link) -static -Ttext-segment=0x1FF000 -u__km_handle_interrupt -u__km_handle_signal -e__start_c__ --gc-sections -zseparate-code -znorelro -zmax-page-size=0x1000 --build-id=none
