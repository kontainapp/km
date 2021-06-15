
# Kontain FAQs 


*   Is Kontain software open source?

    At this time, these are binary-only releases. Kontain code is not open source and is maintained in a private repo. Stay tuned for information about source code availability. In the meantime, we are more than happy to collaborate with people who would like to hack on the code with us! Get in touch by opening an issue or emailing us: community@kontain.app

*   What if I don’t use the latest Linux distro? Do you support earlier Linux distributions?

    We recommend Ubuntu 20 and Fedora 32 distributions because we have done extensive testing with these. Earlier Linux distributions are not fully supported due to limited testing.

*   Is the Kontain Vagrant box only available through VirtualBox? 

    Currently, VirtualBox is the only provider for the `kontain/ubuntu-kkm-beta3` box. If you'd like to suggest a different provider, please submit an issue. 

*   What can I use for virtualization if I don’t have access to KVM? 

    You can install the Kontain kernel module (KKM). For more information, contact community@Kontain.app

*   Can I view Kontain’s interaction with KVM?

    You can use `trace-cmd record` and `trace-cmd report` to observe kvm activity. [For more details on trace-cmd see](https://www.linux-kvm.org/page/Tracing).[https://www.linux-kvm.org/page/Tracing](https://www.linux-kvm.org/page/Tracing)

