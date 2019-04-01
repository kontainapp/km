# Tracing in KM code

We provide a basic facility of per-component tracing.

* Define a tag (string) for your component - see KM_TRACE_VCPU for example.
* Use `km_info(your_tag,... )` and `km_infox(your_tag,...)` to trace to stderr
* Enable trace on runtime by passing `-V<your_tag>` flag. `-V` accepts regex, so if for example you want to trace `gdb` and `kvm` internal KM traces, use `-V'(kvm|gdb)'`
* Default `-V` (with no tags) enables tracing for all tags
