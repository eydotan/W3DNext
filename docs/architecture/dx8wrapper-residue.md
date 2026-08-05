# DX8Wrapper residue inventory (post caller-migration sweep)

Generated 2026-07-15 on branch `renderer-d3d11`, follow-up to the mechanical
`DX8Wrapper::X(...)` -> `g_renderBackend->X(...)` caller sweep. Every remaining
`DX8Wrapper::` reference outside `WW3D2/dx8wrapper.{h,cpp}` and `WW3D2/Backend/`
is listed below, grouped by why it stayed on the static facade. This is the
porting-TODO list for a future D3D11 backend: each group either needs a new
IRenderBackend method (or named hatch), a call-site rewrite beyond a rename,
or is deliberately backend-lifecycle code.

Total remaining references: **2261**

## Absorbed groups (migrated 2026-07-15)

Two groups from the original inventory were absorbed into IRenderBackend and
their call sites migrated (216 refs total):

- **Transform calls (enum type mismatch)** (150 refs: `Set_Transform` 121,
  `Get_Transform` 29) - TransformKind gained `RB_TRANSFORM_TEXTURE0..7`
  (sequential values; the `D3DTS_*` mapping stays inside DX8Backend.cpp), and
  the inline `RB_Texture_Transform(stage)` helper covers the dynamically
  computed `D3DTS_TEXTURE0 + Stage` call sites. The two commented-out
  `Set_Transform` lines in `dx8polygonrenderer.h` remain listed under
  "Comments / string literals".
- **Default-argument or overload mismatch** (66 refs: `Clear` 13, `Set_Gamma` 2,
  `Set_Light` 11, `Set_Render_Target_With_Z` 1, `Set_Vertex_Buffer` 31,
  `Set_Viewport` 8) - the interface now mirrors DX8Wrapper's declared defaults
  (`Clear` dest_alpha/z/stencil, `Set_Gamma` calibrate/uselimit,
  `Set_Vertex_Buffer` stream=0, `Create_Render_Target` format=UNKNOWN,
  `Set_Render_Target_With_Z` ztexture=nullptr). The `Set_Light(n, nullptr)`
  null-light disable became `Disable_Light(index)` so the raw `D3DLIGHT8*`
  overload does not cross the interface. The `Set_Viewport` call sites now
  build a `RenderBackendViewport` instead of a `D3DVIEWPORT8`
  (W3DProfilerFrameCapture converts its device-queried restore viewport once
  at capture).

## D3D8-typed / non-interface entry points (1974 refs)

Why it stayed: D3D8-typed / low-level entry points not on IRenderBackend (raw D3DRS_/D3DTSS_ state, device pointer, DX8 resource creation, caps, color packing, stats) — these are the real porting TODO for a D3D11 backend.

### `Begin_Statistics` (1)

- `Core/Libraries/Source/WWVegas/WW3D2/statistics.cpp`: 369

### `Convert_Color` (87)

- `Core/Libraries/Source/WWVegas/WW3D2/colorspace.h`: 149, 151
- `Core/Libraries/Source/WWVegas/WW3D2/dx8vertexbuffer.cpp`: 653, 668, 692, 704
- `Core/Libraries/Source/WWVegas/WW3D2/line3d.cpp`: 285
- `Core/Libraries/Source/WWVegas/WW3D2/ringobj.cpp`: 553, 555
- `Core/Libraries/Source/WWVegas/WW3D2/seglinerenderer.cpp`: 946, 953, 1007, 1014, 1043, 1072, 1103
- `Core/Libraries/Source/WWVegas/WW3D2/shattersystem.cpp`: 342, 343, 344, 345, 1207, 1208, 1210
- `Core/Libraries/Source/WWVegas/WW3D2/sphereobj.cpp`: 496
- `Core/Libraries/Source/WWVegas/WW3D2/ww3dformat.cpp`: 131
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DAssetManager.cpp`: 1478
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 1382, 1492, 1565, 1597
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/boxrobj.cpp`: 462
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/dazzle.cpp`: 391, 1062, 1105
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/meshmatdesc.cpp`: 752, 753, 757, 781, 786, 798, 803, 812, 817, 826, 831, 868, 873, 895, 899
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/meshmdlio.cpp`: 901, 1267, 1277, 1279, 1329, 1337, 1341
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DAssetManager.cpp`: 1505
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 1429, 1564, 1637, 1669
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/boxrobj.cpp`: 462
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dazzle.cpp`: 393, 1103, 1146
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/linegrp.cpp`: 421, 431, 444, 458
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/meshmatdesc.cpp`: 754, 755, 759, 783, 788, 800, 805, 814, 819, 828, 833
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/meshmdlio.cpp`: 901, 1267, 1277, 1279, 1329, 1337, 1341

### `Convert_Color_Clamp` (11)

- `Core/Libraries/Source/WWVegas/WW3D2/dynamesh.cpp`: 465
- `Core/Libraries/Source/WWVegas/WW3D2/dynamesh.h`: 281, 364, 488
- `Core/Libraries/Source/WWVegas/WW3D2/pointgr.cpp`: 968, 973, 1882, 1887
- `Core/Libraries/Source/WWVegas/WW3D2/streakRender.cpp`: 1353
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DAssetManager.cpp`: 1480
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DAssetManager.cpp`: 1507

### `End_Statistics` (1)

- `Core/Libraries/Source/WWVegas/WW3D2/statistics.cpp`: 385

### `Get_Current_Caps` (66)

- `Core/GameEngineDevice/Source/W3DDevice/Common/System/W3DRadar.cpp`: 84
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp`: 559, 2988, 2994, 2994
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSnow.cpp`: 80, 329
- `Core/Libraries/Source/WWVegas/WW3D2/dx8indexbuffer.cpp`: 296, 490
- `Core/Libraries/Source/WWVegas/WW3D2/dx8renderer.cpp`: 862, 1016, 1214
- `Core/Libraries/Source/WWVegas/WW3D2/dx8vertexbuffer.cpp`: 441, 782
- `Core/Libraries/Source/WWVegas/WW3D2/matpass.cpp`: 123
- `Core/Libraries/Source/WWVegas/WW3D2/sortingrenderer.cpp`: 325, 363
- `Core/Libraries/Source/WWVegas/WW3D2/statistics.cpp`: 295
- `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp`: 688, 1121, 1126, 1127, 1128, 1433, 1718
- `Core/Libraries/Source/WWVegas/WW3D2/texturefilter.cpp`: 123
- `Core/Libraries/Source/WWVegas/WW3D2/textureloader.cpp`: 287, 307, 361
- `Core/Libraries/Source/WWVegas/WW3D2/ww3dformat.cpp`: 310, 325, 326, 334, 366, 368, 370, 374, 376
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp`: 3333
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 2793, 2800, 2804, 2808, 2812
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 1252
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/meshmatdesc.cpp`: 928
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/render2d.cpp`: 595
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`: 413
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp`: 3482
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 3036, 3043, 3047, 3051, 3055
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 1300
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/decalmsh.cpp`: 428
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/meshmatdesc.cpp`: 861, 867, 960
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/meshmdl.cpp`: 642
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/render2d.cpp`: 669
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`: 413, 495, 552, 553, 847

### `Get_Fog_Color` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`: 498
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`: 498

### `Get_MSAA_Mode` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 2057
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 2060

### `Get_Render_State` (2)

- `Core/Libraries/Source/WWVegas/WW3D2/sortingrenderer.cpp`: 233, 695

### `Get_Render_Target_Resolution` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 653
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 654

### `Release_Render_State` (1)

- `Core/Libraries/Source/WWVegas/WW3D2/sortingrenderer.cpp`: 619

### `Set_DX8_Light` (8)

- `Core/Libraries/Source/WWVegas/WW3D2/sortingrenderer.cpp`: 377, 379, 381, 383, 386, 390, 394, 398

### `Set_DX8_Material` (4)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/vertmaterial.cpp`: 903, 937
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/vertmaterial.cpp`: 951, 985

### `Set_DX8_Render_State` (418)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp`: 2469, 2484, 2592, 2653, 2670, 2932
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/FlatHeightMap.cpp`: 543
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/HeightMap.cpp`: 1985, 1988, 2034
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/TerrainTex.cpp`: 467, 538, 539, 540, 732, 733, 734, 874, 875, 876, 893, 894, 895, 1011, 1012, 1013, 1053, 1054, 1055, 1134, 1135, 1136
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp`: 243, 244, 421, 422, 561, 572, 643, 644, 881, 882, 986, 987, 989, 990, 992, 1064, 1154, 1155, 1226, 1271, 1364, 1671, 1685, 1686, 1687, 1710, 1711, 1712, 2286, 2287, 2288, 2290, 2291, 2292, 2387, 2388, 2389, 2400, 2404, 2405, 2514, 2515, 2858, 2869, 3383, 3403, 3404, 3405
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSnow.cpp`: 424, 425, 426, 427, 428, 429, 430, 431, 446, 447
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 1665, 1666, 1667, 1720, 2407, 2408, 2409, 2914, 2935, 3274, 3276, 3321, 3322, 3326, 3327
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWaterTracks.cpp`: 896, 913, 932, 936
- `Core/Libraries/Source/WWVegas/WW3D2/dx8renderer.cpp`: 1721, 1864, 1907, 1911, 1931, 2222, 2230
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp`: 631, 632, 633
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp`: 3335, 3453
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DCustomEdging.cpp`: 369, 370, 371, 379, 380, 381, 385, 386, 409, 410, 411, 412, 413, 414, 424, 425, 426, 427, 428, 429
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 948, 949, 958, 973, 974, 986, 989, 1001, 1017, 1018, 1022, 1027, 1028, 1035, 1036, 1041, 1042, 1235, 1236, 1242, 1243, 1244, 1245, 1246, 1247, 1248, 1249, 1255, 1260, 1261, 1262, 1267, 1268, 1269, 1270, 1271, 1272, 1273, 1276, 1277, 1278, 1285, 1286, 1287, 1288, 1289, 1292, 1349, 1350, 1351, 1352, 1355, 1356, 1357, 1358, 1388, 1404, 1415, 1416, 1417, 1418, 1419, 1420, 1421, 1422, 1423, 1458, 1492, 1510, 1511, 1512, 1513, 1514, 1515, 1516, 1517, 1518, 1547, 1558, 1565, 1597
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DStatusCircle.cpp`: 365, 367, 371, 372, 378, 379
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/render2d.cpp`: 597, 609
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/scene.cpp`: 220, 225, 226, 232, 233
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`: 459, 460, 463, 473, 474, 478, 479, 484, 518, 522, 526, 816, 819, 822, 828, 834, 838
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/vertmaterial.cpp`: 906, 908, 909, 910, 911, 936, 939, 940, 941
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 970, 973, 976, 1030
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp`: 631, 632, 633
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp`: 3484, 3602
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DCustomEdging.cpp`: 369, 370, 371, 379, 380, 381, 385, 386, 409, 410, 411, 412, 413, 414, 424, 425, 426, 427, 428, 429
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 996, 997, 1006, 1021, 1022, 1034, 1037, 1049, 1065, 1066, 1070, 1075, 1076, 1083, 1084, 1089, 1090, 1283, 1284, 1290, 1291, 1292, 1293, 1294, 1295, 1296, 1297, 1303, 1308, 1309, 1310, 1315, 1316, 1317, 1318, 1319, 1320, 1321, 1324, 1325, 1326, 1333, 1334, 1335, 1336, 1337, 1340, 1396, 1397, 1398, 1399, 1402, 1403, 1404, 1405, 1435, 1448, 1449, 1453, 1454, 1469, 1480, 1481, 1482, 1483, 1484, 1485, 1486, 1487, 1488, 1523, 1564, 1582, 1583, 1584, 1585, 1586, 1587, 1588, 1589, 1590, 1619, 1630, 1637, 1669
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DStatusCircle.cpp`: 364, 366, 370, 371, 377, 378
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/render2d.cpp`: 671, 683
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/scene.cpp`: 227, 232, 233, 239, 240
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`: 459, 460, 463, 473, 474, 478, 479, 484, 518, 522, 526, 1018, 1021, 1024, 1030, 1036, 1040
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/vertmaterial.cpp`: 954, 956, 957, 958, 959, 984, 987, 988, 989
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 971, 974, 977, 1031

### `Set_DX8_Texture` (21)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/TerrainTex.cpp`: 567, 576, 585, 594, 603, 612, 1073
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DProfilerFrameCapture.cpp`: 209, 213
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp`: 1309, 1849, 1858, 1867, 1876, 1885, 1894
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSmudge.cpp`: 281
- `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp`: 391, 960, 964, 1256

### `Set_DX8_Texture_Stage_State` (856)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp`: 2470, 2654
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/HeightMap.cpp`: 1984
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/TerrainTex.cpp`: 444, 445, 447, 448, 451, 453, 457, 458, 459, 460, 461, 462, 464, 465, 466, 515, 516, 518, 519, 522, 524, 527, 528, 532, 533, 534, 535, 536, 542, 543, 551, 552, 553, 554, 555, 556, 557, 559, 560, 561, 562, 563, 564, 565, 568, 569, 570, 571, 572, 573, 574, 577, 578, 579, 580, 581, 582, 583, 586, 587, 588, 589, 590, 591, 592, 595, 596, 597, 598, 599, 600, 601, 604, 605, 606, 607, 608, 609, 610, 613, 614, 615, 616, 617, 618, 619, 623, 624, 625, 626, 628, 629, 630, 631, 632, 682, 684, 687, 688, 691, 692, 695, 696, 698, 700, 701, 703, 705, 706, 708, 711, 712, 851, 852, 854, 855, 858, 860, 863, 864, 868, 869, 870, 871, 872, 873, 878, 879, 883, 884, 886, 887, 888, 889, 890, 891, 892, 951, 953, 956, 957, 960, 962, 965, 966, 997, 998, 999, 1000, 1005, 1006, 1008, 1009, 1015, 1016, 1017, 1018, 1019, 1034, 1035, 1036, 1037, 1039, 1040, 1041, 1042, 1044, 1045, 1046, 1047, 1049, 1050, 1051, 1052, 1065, 1066, 1067, 1068, 1069, 1070, 1071, 1112, 1113, 1115, 1116, 1119, 1121, 1124, 1125, 1126, 1129, 1130, 1131, 1132, 1133, 1138, 1139
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp`: 562, 563, 564, 565, 566, 567, 568, 573, 574, 575, 589, 864, 865, 869, 870, 871, 872, 873, 874, 875, 876, 877, 878, 891, 892, 1224, 1225, 1272, 1273, 1311, 1312, 1313, 1314, 1317, 1318, 1365, 1366, 1417, 1418, 1471, 1472, 1591, 1592, 1594, 1595, 1638, 1639, 1640, 1641, 1643, 1644, 1645, 1646, 1649, 1650, 1652, 1653, 1660, 1661, 1664, 1665, 1666, 1667, 1668, 1669, 1670, 1675, 1676, 1679, 1680, 1681, 1682, 1683, 1689, 1690, 1698, 1699, 1700, 1701, 1703, 1705, 1706, 1707, 1726, 1727, 1735, 1736, 1738, 1739, 1740, 1741, 1742, 1744, 1746, 1747, 1756, 1757, 1764, 1765, 1768, 1769, 1806, 1807, 1808, 1809, 1812, 1813, 1814, 1815, 1817, 1818, 1819, 1820, 1823, 1824, 1826, 1827, 1833, 1834, 1835, 1836, 1837, 1838, 1839, 1841, 1842, 1843, 1844, 1845, 1846, 1847, 1850, 1851, 1852, 1853, 1854, 1855, 1856, 1859, 1860, 1861, 1862, 1863, 1864, 1865, 1868, 1869, 1870, 1871, 1872, 1873, 1874, 1877, 1878, 1879, 1880, 1881, 1882, 1883, 1886, 1887, 1888, 1889, 1890, 1891, 1892, 1895, 1896, 1897, 1898, 1899, 1900, 1901, 1905, 1906, 1907, 1908, 1918, 1919, 1920, 1921, 1922, 1923, 2009, 2010, 2011, 2012, 2015, 2016, 2019, 2020, 2021, 2022, 2024, 2025, 2026, 2027, 2030, 2031, 2033, 2034, 2046, 2048, 2050, 2051, 2055, 2056, 2061, 2062, 2064, 2065, 2073, 2075, 2085, 2086, 2092, 2093, 2116, 2117, 2119, 2120, 2122, 2123, 2125, 2126, 2170, 2171, 2173, 2174, 2175, 2176, 2178, 2179, 2180, 2181, 2182, 2183, 2196, 2197, 2199, 2200, 2284, 2302, 2303, 2306, 2307, 2310, 2312, 2314, 2315, 2317, 2318, 2325, 2326, 2328, 2329, 2337, 2339, 2349, 2350, 2352, 2353, 2355, 2356, 2358, 2359, 2392, 2393, 2394, 2395, 2396, 2397, 2399, 2417, 2419, 2421, 2423, 2425, 2426, 2428, 2429, 2430, 2431, 2432, 2433, 2438, 2439, 2450, 2451, 2457, 2458, 2465, 2466, 2479, 2481, 2486, 2487, 2489, 2491, 2493, 2494, 2497, 2499, 2500, 2501, 2502, 2503, 2505, 2506, 2507, 2508, 2509, 2510, 2527, 2528, 2530, 2531, 2890, 2891, 2892, 2893, 2894, 2895, 3208, 3209, 3210, 3211, 3212, 3213, 3214, 3215, 3278, 3279, 3281, 3282, 3292, 3293, 3294, 3295, 3297, 3298, 3299, 3300, 3303, 3304, 3306, 3307, 3314, 3315, 3318, 3319, 3322, 3323, 3324, 3325, 3327, 3328, 3367, 3368, 3370, 3372, 3373, 3376, 3377, 3378, 3379, 3380, 3381, 3382, 3391, 3392, 3393, 3394, 3396, 3398, 3399, 3400, 3418, 3419, 3427, 3428, 3430, 3431, 3432, 3433, 3434, 3436, 3438, 3439, 3449, 3450, 3457, 3458, 3461, 3462, 3563, 3564, 3573, 3574, 3576, 3577, 3580, 3581, 3583, 3584, 3587, 3589, 3597, 3598, 3634, 3635, 3636, 3637, 3653, 3655, 3657, 3658, 3662, 3663, 3680, 3682, 3684, 3685, 3689, 3690, 3720, 3721, 3723, 3724, 3726, 3727, 3729, 3730
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSmudge.cpp`: 454, 455, 456, 457, 458, 459, 467, 544, 548, 553, 554
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DTreeBuffer.cpp`: 1709, 1710
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 228, 232, 233, 234, 235, 236, 249, 250, 252, 254, 255, 256, 1641, 1642, 1645, 1647, 1653, 1654, 1657, 1658, 1659, 1661, 1662, 1663, 1719, 2430, 2431, 2432, 2433, 2988, 2989, 2990, 3006, 3007, 3009, 3011, 3012, 3013
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWaterTracks.cpp`: 906, 907, 908, 909
- `Core/Libraries/Source/WWVegas/WW3D2/texturefilter.cpp`: 90, 91, 92, 97, 101, 108, 112, 301
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DCustomEdging.cpp`: 398, 399, 401, 402, 403, 404, 405, 406, 407
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/mapper.cpp`: 120, 123, 168, 171, 220, 223, 343, 346, 408, 411, 471, 474, 549, 552, 580, 583, 600, 603, 657, 659, 662, 696, 699, 728, 731, 762, 765, 790, 793, 820, 823, 893, 896, 945, 946, 947, 948
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/matrixmapper.cpp`: 217, 218, 228, 229, 241, 242, 254, 255
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/render2d.cpp`: 598, 599, 600, 601, 603, 604, 605, 610, 611, 612, 615
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`: 670, 671, 672, 673, 674, 675, 762, 763, 764, 806, 807, 808
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/vertmaterial.cpp`: 918, 919, 945, 946
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DCustomEdging.cpp`: 398, 399, 401, 402, 403, 404, 405, 406, 407
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/mapper.cpp`: 96, 99, 234, 237, 629, 632, 655, 658, 712, 714, 717, 813, 816, 828, 831, 843, 846, 872, 875, 901, 904, 1078, 1079, 1080, 1081, 1237, 1240, 1281, 1284
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/matrixmapper.cpp`: 237, 238, 248, 249, 261, 262, 274, 275
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/render2d.cpp`: 672, 673, 674, 675, 677, 678, 679, 684, 685, 686, 689
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`: 938, 939, 947, 948, 949, 950, 975, 976, 977, 978, 979, 980, 987, 988, 989, 990, 991, 992
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/vertmaterial.cpp`: 966, 967, 993, 994

### `Set_MSAA_Mode` (8)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 2037, 2041, 2045, 2049
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 2040, 2044, 2048, 2052

### `Set_Render_State` (1)

- `Core/Libraries/Source/WWVegas/WW3D2/sortingrenderer.cpp`: 617

### `Set_Render_Target` (4)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DProfilerFrameCapture.cpp`: 167, 215
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 1516
- `Core/Libraries/Source/WWVegas/WW3D2/texproject.cpp`: 1167

### `_Copy_DX8_Rects` (16)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DProfilerFrameCapture.cpp`: 156, 220
- `Core/Libraries/Source/WWVegas/WW3D2/render2dsentence.cpp`: 373
- `Core/Libraries/Source/WWVegas/WW3D2/surfaceclass.cpp`: 464
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 3045
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DShroud.cpp`: 483, 494, 714
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1373, 1723
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 3396
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DShroud.cpp`: 483, 494, 714
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1376, 1726

### `_Create_DX8_Cube_Texture` (6)

- `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp`: 1373, 1530, 1815
- `Core/Libraries/Source/WWVegas/WW3D2/textureloader.cpp`: 2198, 2301, 2370

### `_Create_DX8_Surface` (13)

- `Core/Libraries/Source/WWVegas/WW3D2/surfaceclass.cpp`: 169, 175
- `Core/Libraries/Source/WWVegas/WW3D2/textureloader.cpp`: 597
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 3044
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DShroud.cpp`: 160, 163
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1372, 1722
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 3395
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DShroud.cpp`: 160, 163
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1375, 1725

### `_Create_DX8_Texture` (14)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 466, 552
- `Core/Libraries/Source/WWVegas/WW3D2/dx8texman.h`: 107
- `Core/Libraries/Source/WWVegas/WW3D2/missingtexture.cpp`: 64
- `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp`: 625, 785
- `Core/Libraries/Source/WWVegas/WW3D2/textureloader.cpp`: 263, 438, 498, 1520, 1615, 1691, 1761, 1812

### `_Create_DX8_Volume_Texture` (4)

- `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp`: 1657
- `Core/Libraries/Source/WWVegas/WW3D2/textureloader.cpp`: 2565, 2666, 2738

### `_Create_DX8_ZTexture` (2)

- `Core/Libraries/Source/WWVegas/WW3D2/dx8texman.h`: 144
- `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp`: 1219

### `_Enable_Triangle_Draw` (2)

- `Core/Libraries/Source/WWVegas/WW3D2/sortingrenderer.cpp`: 626, 628

### `_Get_D3D8` (1)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp`: 2929

### `_Get_D3D_Device8` (218)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp`: 1427, 1427
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/HeightMap.cpp`: 2059, 2065, 2069, 2189, 2195, 2199
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DMouse.cpp`: 111, 392, 488
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DProfilerFrameCapture.cpp`: 41, 85, 129, 170
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp`: 92, 93, 191, 202, 252, 330, 341, 425, 426, 454, 455, 457, 458, 459, 460, 461, 470, 471, 478, 519, 578, 654, 781, 795, 797, 893, 950, 962, 1163, 1588, 1589, 1659, 1674, 1721, 1730, 1754, 1762, 1830, 1831, 1925, 1926, 1933, 1936, 1939, 2006, 2007, 2057, 2058, 2059, 2079, 2083, 2090, 2100, 2108, 2109, 2111, 2113, 2114, 2185, 2194, 2235, 2323, 2347, 2604, 2615, 2634, 2789, 2837, 2884, 2931, 3050, 3054, 3275, 3276, 3321, 3420, 3440, 3447, 3455, 3479, 3482, 3485, 3488, 3638, 3659, 3686, 3696, 3698, 3700, 3702, 3704, 3706, 3712, 3713, 3715, 3717, 3718
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSmudge.cpp`: 141, 264, 543, 546, 547
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSnow.cpp`: 82, 317, 433, 434
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DTreeBuffer.cpp`: 1125, 1129, 1727, 1729, 1734, 1748, 1752, 1756, 1757, 1767, 1785, 1786, 1787, 1788
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 231, 243, 247, 280, 281, 875, 939, 952, 967, 1065, 2437, 2439, 2449, 2916, 2918, 2919, 2924, 2928, 2931, 2937, 2969, 2984, 2999, 3004, 3035, 3036, 3281, 3282, 3311, 3315, 3318, 3335, 3336, 3342, 3345, 3347, 3351
- `Core/Libraries/Source/WWVegas/WW3D2/dx8indexbuffer.cpp`: 300, 322
- `Core/Libraries/Source/WWVegas/WW3D2/dx8vertexbuffer.cpp`: 445, 466, 469
- `Core/Libraries/Source/WWVegas/WW3D2/dx8webbrowser.cpp`: 98
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp`: 274, 372, 686, 820, 1151
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp`: 1227, 1287, 1424, 3192, 3280, 3334, 3598
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 1853, 1853
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 1225, 1254
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DShroud.cpp`: 529, 529
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 814, 814
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp`: 274, 372, 686, 820, 1151
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp`: 1333, 1393, 1530, 3341, 3429, 3483, 3747
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 2019, 2019
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 1273, 1302
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DShroud.cpp`: 529, 529
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 815, 815

### `_Get_DX8_Back_Buffer` (9)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DProfilerFrameCapture.cpp`: 108
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSmudge.cpp`: 84, 318
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 3039
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1367, 1717
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 3390
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1370, 1720

### `_Get_DX8_Transform` (25)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/TerrainTex.cpp`: 715, 969
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp`: 1234, 1326, 1415, 1695, 2040, 2160, 2295, 2410, 2472, 3218, 3336, 3388, 3604, 3647, 3674
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DTreeBuffer.cpp`: 1718, 1719, 1720
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 259, 1624, 1826, 1827, 3016

### `_Is_Triangle_Draw_Enabled` (13)

- `Core/Libraries/Source/WWVegas/WW3D2/sortingrenderer.cpp`: 625
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp`: 512, 751
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp`: 1271, 1371, 3246
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 1281
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp`: 512, 751
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp`: 1377, 1477, 3395
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 1329

### `_Set_DX8_Transform` (35)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/TerrainTex.cpp`: 725, 727, 1002, 1021
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp`: 1262, 1354, 1463, 1724, 1733, 1770, 2068, 2071, 2095, 2172, 2332, 2335, 2442, 2460, 2517, 3246, 3364, 3416, 3425, 3463, 3632, 3661, 3688
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 268, 1650, 1967, 1968, 1992, 3025
- `Core/Libraries/Source/WWVegas/WW3D2/sortingrenderer.cpp`: 368, 369

### `getBackBufferFormat` (7)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp`: 2445, 2608
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/FlatHeightMap.cpp`: 542
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/HeightMap.cpp`: 2033
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 3273
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 2791
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 3034

### `stats` (114)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp`: 2945
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/FlatHeightMap.cpp`: 486
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/HeightMap.cpp`: 1399, 1937
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 1447, 1543
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 1142, 1143, 1144, 1145, 1146, 1147, 1151, 1152, 1153, 1154, 1155, 1156, 1160, 1161, 1162, 1163, 1164, 1165, 1169, 1170, 1171, 1172, 1173, 1174, 1178, 1179, 1180, 1181, 1182, 1183, 1184, 1188, 1189, 1190, 1191, 1192, 1193, 1194, 1201, 1202, 1203, 1205, 1206, 1207, 1208, 1209, 1210, 1211, 1547, 1549, 1770, 2009
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DInGameUI.cpp`: 425
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 1120
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`: 1219, 1220, 1221, 1222, 1223, 1224, 1228, 1229, 1230, 1231, 1232, 1233, 1237, 1238, 1239, 1240, 1241, 1242, 1246, 1247, 1248, 1249, 1250, 1251, 1255, 1256, 1257, 1258, 1259, 1260, 1261, 1265, 1266, 1267, 1268, 1269, 1270, 1271, 1278, 1279, 1280, 1282, 1283, 1284, 1285, 1286, 1287, 1288, 1643, 1645, 1936, 2232
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DInGameUI.cpp`: 425
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 1168

## Device / init / reset lifecycle (66 refs)

Why it stayed: Device / init / reset / shutdown lifecycle — can run while g_renderBackend is null (Init_Render_Backend is the LAST step of Do_Onetime_Device_Dependent_Inits; Shutdown_Render_Backend the FIRST of shutdowns); deliberately stays static.

### `Get_Device_Resolution` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 672
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 673

### `Get_Device_Resolution_Height` (4)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1294, 1298
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1297, 1301

### `Get_Device_Resolution_Width` (4)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1293, 1297
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1296, 1300

### `Get_Render_Device` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 552
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 553

### `Get_Render_Device_Count` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 591
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 592

### `Get_Render_Device_Desc` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 571
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 572

### `Get_Render_Device_Name` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 610
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 611

### `Get_Swap_Interval` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1236
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1239

### `Get_Texture_Bitdepth` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 2028
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 2031

### `Init` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 278
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 279

### `Is_Initted` (7)

- `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp`: 688, 1121, 1433, 1718
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/meshmatdesc.cpp`: 861, 867, 960

### `Is_Windowed` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 508
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 509

### `Registry_Load_Render_Device` (4)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 736, 746
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 737, 747

### `Registry_Save_Render_Device` (4)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 691, 713
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 692, 714

### `Reset_Device` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 824
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 825

### `SetCleanupHook` (1)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp`: 313

### `Set_Any_Render_Device` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 423
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 424

### `Set_Device_Resolution` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 628
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 629

### `Set_Next_Render_Device` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 469
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 470

### `Set_Render_Device` (4)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 400, 446
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 401, 447

### `Set_Swap_Interval` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1218
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1221

### `Set_Texture_Bitdepth` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 2023
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 2026

### `Shutdown` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 368
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 369

### `Toggle_Windowed` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 528
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 529

### `_Get_Main_Thread_ID` (4)

- `Core/Libraries/Source/WWVegas/WW3D2/textureloader.cpp`: 344, 1805, 2187, 2555

## Pre-init reachable (left for safety) (2 refs)

Why it stayed: Reachable before backend init: ShaderClass::Apply runs inside DX8Wrapper's own state-flush machinery (e.g. Apply_Default_State during device bring-up) — unsure-safe, left on the static.

### `Get_Fog_Enable` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`: 495
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`: 495

## Device-reset resource paths (5 refs)

Why it stayed: Device-reset resource re-acquisition paths (ReAcquireResources) — reset paths stay on the statics per the migration rules.

### `Create_Render_Target` (5)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 915
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp`: 265, 271
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp`: 265, 271

## Tools (not in build oracle) (175 refs)

Why it stayed: Tool code (WorldBuilder / W3DView) — outside the game build oracle (z_generals / g_generals); left untouched wholesale, migrate when those targets are in the verified build loop.

### `Apply_Render_State_Changes` (4)

- `Generals/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 1626, 1631
- `GeneralsMD/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 2081, 2087

### `Create_Render_Target` (2)

- `Generals/Code/Tools/WorldBuilder/src/ObjectPreview.cpp`: 213
- `GeneralsMD/Code/Tools/WorldBuilder/src/ObjectPreview.cpp`: 213

### `Draw_Triangles` (22)

- `Generals/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 1705, 1707, 1762, 1771, 1821, 1838, 1855, 1863, 1880, 1898, 1916
- `GeneralsMD/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 2221, 2223, 2278, 2287, 2334, 2351, 2367, 2375, 2389, 2404, 2422

### `SetCleanupHook` (2)

- `Generals/Code/Tools/WorldBuilder/src/wbview3d.cpp`: 2282
- `GeneralsMD/Code/Tools/WorldBuilder/src/wbview3d.cpp`: 2375

### `Set_DX8_Render_State` (18)

- `Generals/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 1854, 1878, 1879, 1895, 1896, 1897, 1913, 1914, 1915
- `GeneralsMD/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 2366, 2387, 2388, 2401, 2402, 2403, 2419, 2420, 2421

### `Set_Gamma` (5)

- `Core/Tools/W3DView/GammaDialog.cpp`: 97, 106
- `Core/Tools/W3DView/MainFrm.cpp`: 590, 4211, 4213

### `Set_Index_Buffer` (37)

- `Core/Tools/W3DView/ScreenCursor.cpp`: 304
- `Generals/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 1702, 1713, 1719, 1760, 1770, 1774, 1779, 1819, 1827, 1836, 1839, 1845, 1852, 1861, 1869, 1876, 1886, 1893, 1904, 1911, 1920
- `GeneralsMD/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 2086, 2234, 2276, 2286, 2290, 2340, 2349, 2352, 2364, 2373, 2385, 2399, 2410, 2417, 2426

### `Set_Material` (3)

- `Core/Tools/W3DView/ScreenCursor.cpp`: 299
- `Generals/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 1628
- `GeneralsMD/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 2083

### `Set_Render_Target` (2)

- `Generals/Code/Tools/WorldBuilder/src/ObjectPreview.cpp`: 251
- `GeneralsMD/Code/Tools/WorldBuilder/src/ObjectPreview.cpp`: 251

### `Set_Render_Target_With_Z` (2)

- `Generals/Code/Tools/WorldBuilder/src/ObjectPreview.cpp`: 221
- `GeneralsMD/Code/Tools/WorldBuilder/src/ObjectPreview.cpp`: 221

### `Set_Shader` (15)

- `Core/Tools/W3DView/ScreenCursor.cpp`: 300
- `Generals/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 1629, 1837, 1853, 1862, 1877, 1894, 1912
- `GeneralsMD/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 2084, 2350, 2365, 2374, 2386, 2400, 2418

### `Set_Texture` (3)

- `Core/Tools/W3DView/ScreenCursor.cpp`: 301
- `Generals/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 1630
- `GeneralsMD/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 2085

### `Set_Transform` (10)

- `Generals/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 1701, 1761, 1765, 1820, 1830
- `GeneralsMD/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 2219, 2277, 2281, 2333, 2343

### `Set_Vertex_Buffer` (44)

- `Core/Tools/W3DView/ScreenCursor.cpp`: 303
- `Generals/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 1680, 1683, 1712, 1744, 1747, 1766, 1768, 1778, 1799, 1802, 1826, 1835, 1840, 1844, 1851, 1860, 1868, 1875, 1885, 1892, 1903, 1910, 1921
- `GeneralsMD/Code/Tools/WorldBuilder/src/DrawObject.cpp`: 2097, 2185, 2201, 2230, 2260, 2263, 2282, 2284, 2313, 2316, 2341, 2348, 2353, 2363, 2372, 2384, 2398, 2409, 2416, 2427

### `_Get_D3D_Device8` (6)

- `Generals/Code/Tools/WorldBuilder/src/ObjectPreview.cpp`: 88
- `Generals/Code/Tools/WorldBuilder/src/wbview3d.cpp`: 520, 2180
- `GeneralsMD/Code/Tools/WorldBuilder/src/ObjectPreview.cpp`: 88
- `GeneralsMD/Code/Tools/WorldBuilder/src/wbview3d.cpp`: 538, 2272

## Comments / string literals (39 refs)

Why it stayed: Inert references inside comments or string literals — no code change wanted in a mechanical sweep.

### `Apply_Render_State_Changes` (1)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp`: 1315

### `Convert_Color` (2)

- `Core/Libraries/Source/WWVegas/WW3D2/streakRender.cpp`: 1281, 1290

### `End_Scene` (2)

- `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1107
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp`: 1110

### `Reset_Device` (2)

- `Generals/Code/Main/WinMain.cpp`: 464
- `GeneralsMD/Code/Main/WinMain.cpp`: 466

### `Set_DX8_Clip_Plane` (1)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 1616

### `Set_DX8_Render_State` (12)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 1617, 1716
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp`: 708, 709
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 998, 1006
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`: 825
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp`: 708, 709
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`: 1046, 1054
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`: 1027

### `Set_DX8_Texture_Stage_State` (1)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSmudge.cpp`: 466

### `Set_Material` (1)

- `Core/Libraries/Source/WWVegas/WW3D2/dx8renderer.cpp`: 1718

### `Set_Shader` (11)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSmudge.cpp`: 448
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 2192, 2398
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp`: 694
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DBridgeBuffer.cpp`: 1159
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DCustomEdging.cpp`: 362
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DRoadBuffer.cpp`: 3269
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp`: 694
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DBridgeBuffer.cpp`: 1162
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DCustomEdging.cpp`: 362
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DRoadBuffer.cpp`: 3342

### `Set_Texture` (2)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`: 2193, 3382

### `Set_Transform` (2)

- `Core/Libraries/Source/WWVegas/WW3D2/dx8polygonrenderer.h`: 114, 140

### `_Get_D3D_Device8` (2)

- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp`: 456
- `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DSmudge.cpp`: 542

