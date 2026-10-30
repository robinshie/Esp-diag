# Official H3C manuals

These are official H3C pages. The repository stores links and local notes only;
download the manual revision that matches the switch software release.

## Product and installation

- [S5120V2-LI product document center](https://www.h3c.com/cn/Service/Document_Software/Document_Center/Switches/Catalog/S5120/S5120V2-LI/)
- [S5120V2-LI software and release-note downloads](https://www.h3c.com/cn/Service/Document_Software/Software_Download/Switches/Catalog/H3C_S5120V2/H3C_S5120V2_LI/)
- [Installation guide covering S5120V2-LI models](https://www.h3c.com/cn/d_202505/2475787_30005_0.htm)

## Configuration and commands

- [R612x configuration guide](https://www.h3c.com/cn/Service/Document_Software/Document_Center/Home/Switches/00-Public/Configure/Operation_Manual/H3C_S5130S-SI%5BLI%5D_S5120V2-SI%5BLI%5D-CG-R612x-6W103/)
- [R612x command reference introduction](https://www.h3c.com/cn/Service/Document_Software/Document_Center/Home/Switches/00-Public/Command/Command_Manual/H3C_S5130S-SI%5BLI%5D_S5120V2-SI%5BLI-CR-R612x-6W103/00/)
- [R611x configuration guide introduction](https://www.h3c.com/cn/Service/Document_Software/Document_Center/Switches/Catalog/S5120/S5120V2-LI/Configure/Operation_Manual/H3C_S5560S-SI_S5130S-SI%5BLI%5D-Release_611x-6W103/00/)
- [R611x command reference](https://www.h3c.com/cn/d_201906/1207774_30005_0.htm)

## System log references

- [S5120V2-LI System Log Messages Reference 6W100](https://www.h3c.com/en/Support/Resource_Center/EN/Switches/Catalog/S5120V2/S5120V2-LI/Technical_Documents/Reference_Guides/Log_Message_References/H3C_S5120V2-LI_Messages-6W100/)
- [Fixed-Port Switches System Log Messages Reference R63xx 6W101](https://www.h3c.com/en/Support/Resource_Center/EN/Home/Switches/00-Public/Reference_Guides/Log_Message_References/H3C_Fixed-Port_Campus_SM-R63xx/)

## Version selection

| Switch release | Preferred log reference |
| --- | --- |
| Before R6308P01 | Dedicated S5120V2-LI 6W100 reference |
| R6308P01 and later | Fixed-Port Switches R63xx reference |

The official format for console, monitor, log buffer, and log file output is:

```text
Prefix Timestamp Sysname MODULE/severity/MNEMONIC: CONTENT
```

The log-host format adds PRI and the H3C vendor marker:

```text
<PRI>TIMESTAMP Sysname %%10MODULE/severity/MNEMONIC: location; CONTENT
```
