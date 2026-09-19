# Emberhuk
Windows Kernel Telemetry Logger


<img width="1415" height="751" alt="image" src="https://github.com/user-attachments/assets/c44d0f91-0f9b-4bb3-87aa-b9deacbd8ee0" />



### To load the minifilter driver

```
rundll32.exe setupapi.dll,InstallHinfSection DefaultInstall 132 .\EmberhukMinifilter.inf

fltmc load EmberhukMinifilter

fltmc unload EmberhukMinifilter
```
