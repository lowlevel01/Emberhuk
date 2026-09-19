# Emberhuk
Windows Kernel Mode Telemetry Logger

### What does it log?

- Process, Thread, Image loads, Registry writes.
- File Create with a minifilter driver
- Outbound network connections with a WFP Callout driver.

<img width="1415" height="751" alt="image" src="https://github.com/user-attachments/assets/c44d0f91-0f9b-4bb3-87aa-b9deacbd8ee0" />

### Usermode consumer
- The drivers write event structs to a ring ruffer
- An MDL is created for the ring buffer which is mapped to address space of the user mode client.
- user mode client reads and parses the event structures.
- For the case of the minifilter, usermode reads events through a communication port.


### To load the minifilter driver

```
rundll32.exe setupapi.dll,InstallHinfSection DefaultInstall 132 .\EmberhukMinifilter.inf

fltmc load EmberhukMinifilter
```

To unload:
```
fltmc unload EmberhukMinifilter
```

### Contribution
PRs are very welcome, code still needs so much work including cleaning and handling more telemetry cases.
