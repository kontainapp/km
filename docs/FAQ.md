
# Kontain FAQs


*   Is Kontain software open source?

    Kontain code is open source under Apache 2.0 license.

    We also maintain binary releases in the form of vagrant boxes (Fedora 32 and Ubuntu 2004) and an EC2 AMI.

    We are more than happy to collaborate with people who would like to hack on the code with us! Get in touch by opening an issue or emailing us: community@kontain.app

*   What if I don’t use the latest Linux distro? Do you support earlier Linux distributions?

    We recommend Ubuntu 20 and Fedora 32 distributions because we have done extensive testing with these. Earlier Linux distributions are not fully supported due to limited testing.

*   Is the Kontain Vagrant box only available through VirtualBox?

    Currently, VirtualBox is the only provider for the `kontain/ubuntu2004-kkm-beta3` and `kontain/fedora32-kkm-beta3` boxes.
    If you'd like to suggest a different provider, please submit an issue.

*   What can I use for virtualization if I don’t have access to KVM?

    You can install the Kontain kernel module (KKM). For more information, contact community@Kontain.app

*   Can I view Kontain’s interaction with KVM?

    You can use `trace-cmd record` and `trace-cmd report` to observe kvm activity. For more details on trace-cmd, see [https://www.linux-kvm.org/page/Tracing](https://www.linux-kvm.org/page/Tracing).

