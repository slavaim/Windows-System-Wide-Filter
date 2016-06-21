# Windows-System-Wide-Filter

##License
  The license model is a BSD Open Source License. This is a non-viral license, only asking that if you use it, you acknowledge the authors, in this case Slava Imameev.

##Design
  This is a set of Windows drivers that were developed to filter all input-output in the system including file system access filtering. The drivers are WDM drivers with some hooking techniques that allows drivers to be loaded by demand and control already started devices and mounted file systems.  
  
  I developed this project in 2006-2007. The drivers were tested but some functionality is missing. The project is closed and will not be supported.  
  
##Directory structure

| Directory | Description|
| ------------- |:-------------|
| common/objects | an object manager, similar to Windows Object Manager|
| common/wthreads | a worker threads library |
| common/hash | a hash table implementation |
| o_core | a core that provides services such as filesystem minifilter emulation, IRP completion hooking, PnP tree |
| o_hooker | a driver objects hooker |
| o_filter | IRP filtering functionality |

Slava Imameev  
Sydney  
September 2015   
