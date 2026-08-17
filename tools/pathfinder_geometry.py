"""Classic Geometry Dash gameplay geometry measured by camila314/pathfinder.

Values are FULL object widths/heights in GD world units before object scale.
Unknown IDs are deliberately absent: decoration must never silently become
collision.  Saw values are ellipse radii because Pathfinder's Sawblade::touching
uses its stored size.x directly as the radius.
"""

BLOCK_SIZES = {}

def _put(dst, ids, wh):
    for i in ids:
        dst[i] = wh

_put(BLOCK_SIZES, list(range(1,5))+[6,7,63]+list(range(69,73))+list(range(74,79))+
     list(range(81,84))+list(range(90,97))+list(range(116,120))+[121,122,146]+
     list(range(160,164))+list(range(165,170))+[173,175]+list(range(207,211))+
     [212,213]+list(range(247,251))+list(range(252,259))+[260,261]+
     list(range(263,266))+list(range(267,273))+[274,275,467,469,470,471,1203,1204,
     1209,1210,1221,1222,1226], (30.0,30.0))
_put(BLOCK_SIZES, [64,195,206,220,661,1155,1156,1157,1208,1910], (15.0,15.0))
_put(BLOCK_SIZES, [40,147,215,369,370,1903,1904,1905], (30.0,14.0))
_put(BLOCK_SIZES, [170,171,172,174,192], (30.0,21.0))
_put(BLOCK_SIZES, [468,475,1260], (30.0,1.5))
_put(BLOCK_SIZES, [62,65,66,68], (30.0,16.0))
_put(BLOCK_SIZES, [1202,1262], (30.0,3.0))
_put(BLOCK_SIZES, [1220,1264], (30.0,6.0))
_put(BLOCK_SIZES, [196,219,1911], (15.0,8.0))
_put(BLOCK_SIZES, [204], (8.0,15.0))
_put(BLOCK_SIZES, [662,663,664], (30.0,15.0))
_put(BLOCK_SIZES, [1561], (30.0,10.0))
_put(BLOCK_SIZES, [1567], (15.0,10.0))
_put(BLOCK_SIZES, [1566], (12.0,12.0))
_put(BLOCK_SIZES, [1565], (17.0,17.0))
_put(BLOCK_SIZES, [1227], (30.0,7.0))
_put(BLOCK_SIZES, [328], (22.0,22.0))
_put(BLOCK_SIZES, [197], (22.0,21.0))
_put(BLOCK_SIZES, [194], (21.0,21.0))
_put(BLOCK_SIZES, [176], (14.0,21.0))
_put(BLOCK_SIZES, [1562], (30.0,2.0))
_put(BLOCK_SIZES, [1343], (25.0,3.0))
_put(BLOCK_SIZES, [1340], (27.0,2.0))
_put(BLOCK_SIZES, [34], (37.0,23.0))
# Pathfinder models 143 as a 30x30 breakable block. Until dash-break logic is
# implemented it remains solid rather than the old, incorrect spike hazard.
_put(BLOCK_SIZES, [143], (30.0,30.0))

HAZARD_SIZES = {}
_put(HAZARD_SIZES, [720,991,1731,1733], (2.40039063,3.20001221))
_put(HAZARD_SIZES, [61,446,1719,1728], (9.0,7.2))
_put(HAZARD_SIZES, [365,667,1716,1730], (9.0,6.0))
_put(HAZARD_SIZES, [392,458,459], (2.6,4.8))
_put(HAZARD_SIZES, [8,144,177,216], (6.0,12.0))
_put(HAZARD_SIZES, [103,145,218], (4.0,7.6))
_put(HAZARD_SIZES, [39,205,217], (6.0,5.6))
_put(HAZARD_SIZES, [768,1727], (4.5,5.2))
_put(HAZARD_SIZES, [447,1729], (5.2,7.2))
_put(HAZARD_SIZES, [135,1711], (14.1,20.0))
_put(HAZARD_SIZES, [422,1726], (6.0,4.4))
_put(HAZARD_SIZES, [244,1721], (6.0,6.8))
_put(HAZARD_SIZES, [243,1720], (6.0,7.2))
_put(HAZARD_SIZES, [421,1725], (9.0,5.2))
_put(HAZARD_SIZES, [9,1715], (9.0,10.8))
_put(HAZARD_SIZES, [989,1732], (9.0,12.0))
_put(HAZARD_SIZES, [1714], (11.4,16.4))
_put(HAZARD_SIZES, [1712], (13.5,22.4))
_put(HAZARD_SIZES, [368,1722], (9.0,4.0))
_put(HAZARD_SIZES, [1713], (11.7,20.0))
_put(HAZARD_SIZES, [178], (6.0,6.4))
_put(HAZARD_SIZES, [919], (25.0,6.0))
_put(HAZARD_SIZES, [179], (4.0,8.0))

SAW_RADII = {}
_put(SAW_RADII, [88,186,740,1705], 32.3)
_put(SAW_RADII, [89,1706], 21.6)
_put(SAW_RADII, [98,1707], 12.0)
_put(SAW_RADII, [183], 15.660001)
_put(SAW_RADII, [184], 20.4)
_put(SAW_RADII, [185], 2.8500001)
_put(SAW_RADII, [187,741], 21.960001)
_put(SAW_RADII, [188,742], 12.6000004)
_put(SAW_RADII, [397,1708], 28.9)
_put(SAW_RADII, [398,1709], 17.44)
_put(SAW_RADII, [399,1710], 12.900001)
_put(SAW_RADII, [675,1734], 32.0)
_put(SAW_RADII, [676,1735], 17.5100002)
_put(SAW_RADII, [677,1736], 12.479999)
_put(SAW_RADII, [678], 30.4)
_put(SAW_RADII, [679], 18.54)
_put(SAW_RADII, [680], 10.8)
_put(SAW_RADII, [918], 24.0)
_put(SAW_RADII, [1582,1583], 4.0)
_put(SAW_RADII, [1619], 25.0)
_put(SAW_RADII, [1620], 15.0)
_put(SAW_RADII, [1701,1702,1703], 6.0)

# Not yet emitted as solids: they need triangular collision rather than AABBs.
SLOPE_SOLID_SIZES = {}
_put(SLOPE_SOLID_SIZES, [289,294,299,305,309,315,321,326,331,337,343,349,353,371,
    483,492,651,665,673,709,711,726,728,886,1338,1341,1344,1723,1743,1745,1747,
    1749,1906], (30.0,30.0))
_put(SLOPE_SOLID_SIZES, [291,295,301,307,311,317,323,327,333,339,345,351,355,367,
    372,484,493,652,666,674,710,712,727,729,887,1339,1342,1345,1724,1744,1746,
    1748,1750,1907], (60.0,30.0))
SLOPE_HAZARD_SIZES = {363:(30.0,30.0),1717:(30.0,30.0),364:(60.0,30.0),366:(60.0,30.0),1718:(60.0,30.0)}
