%rename link old_link

*lib:
-lruntime

*libgcc:
-lgcc -lgcc_eh

*startfile:
crtbeginT.o%s

*endfile:
crtend.o%s

*link:
-static -Ttext-segment=0x1FF000 -u__km_handle_interrupt -u__km_handle_signal -e__start_c__ --gc-sections -z norelro %(old_link) --build-id=none
