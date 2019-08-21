%rename link old_link

*lib:
-lruntime -lkmpthread -lruntime

*libgcc:
-lgcc -lgcc_eh

*startfile:
crtbeginT.o%s

*endfile:
crtend.o%s

*link:
%(old_link) -static -Ttext-segment=0x200000 -umain -u__km_sigreturn -u__km_handle_interrupt -e__start_c__ --gc-sections -zseparate-code -znorelro -zmax-page-size=0x1000
