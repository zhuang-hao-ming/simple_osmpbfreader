osm pbf格式

pbf即protocolbuffer binary format是基于protocolbuffer的osm文件格式.

设计它的目的是为了替代原生的xml文件格式.

它的体积是gzipped xml的一半,bzipped xml的70%,它的写速度是gzipped xml的5倍, 读速度是gzipped xml的6倍.


pbf在设计的时候考虑了扩展性和灵活性.


逻辑上看一个pbf文件由多个*fileblock*组成, 可以支持独立地访问任意的*fileblock*而忽略其它*fileblock*.


一般一个*fileblock*包含8k个osm实体.

tag不会被硬编码存取,而是将所有的字符串存在一个列表中,通过索引来标识tag的key和value.

node,way和relation的id使用uint64_t

主流的序列化器(osmosis)在产生pbf文件的时候,会保留osm实体以及实体内部tag的顺序.

内部的经纬度分辨率是1 nano degree的倍数默认是100 nano degree, 在赤道上大概是1cm.

时间的分辨率是1 millisecond的倍数默认是1000ms


[osmbinary](https://github.com/scrosby/OSM-binary) 仓库保存了格式的.proto文件

## 详细设计

PBF格式底层使用protobuf格式,protobuf格式是谷歌推出的一种消息存储交换格式,它具有体积小速度快的容易使用的优点.

体积小在于它使用了独特的varint编码,使得小整数占用的空间较小.

易使用在于它可以自动生成读写文件的代码.不需要程序员手动编写读写程序.


文件逻辑上由多个fileblock组成,第一个fileblock作为头部,其它fileblock存储数据,这样设计的目的是为了,可以随机访问一个fileblock而忽略不想处理的fileblock.


一个fileblock的组成如下

int32_t 一个4字节的整数,代表BlobHeader的大小,它的字节序是网络字节序 ==这个int32_t不属于protobuf的一部分(不是message)==

BlobHeader 一个protobuf message

Blob 一个protobuf message

```
message BlobHeader {
  required string type = 1;
  optional bytes indexdata = 2;
  required int32 datasize = 3;
}
```

type: fileblock的类型,现在只有两种, OSMHeader和OSMData分别代表头部和数据.

indexdata: Blob的元数据,可能是bounding box message. 也可以做其它用途.

datasize: Blob的大小


```
message Blob {
  optional bytes raw = 1; // No compression
  optional int32 raw_size = 2; // When compressed, the uncompressed size

  // Possible compressed versions of the data.
  optional bytes zlib_data = 3;

  // PROPOSED feature for LZMA compressed data. SUPPORT IS NOT REQUIRED.
  optional bytes lzma_data = 4;

  // Formerly used for bzip2 compressed data. Depreciated in 2010.
  optional bytes OBSOLETE_bzip2_data = 5 [deprecated=true]; // Don't reuse this tag number.
}

```

raw: 如果有, 是未压缩的PrimitiveBlock(HeaderBlock) message的二进制字节数组

raw_size: 未压缩的PrimitiveBlock(HeaderBlock) message的二进制字节数组的大小

zlib_data: 如果有, 是zlib压缩的PrimitiveBlock(HeaderBlock) message的二进制字节数组

其余两个字段分别未实现和弃用.


==为了能够检测不合法文件, 规定, BlobHeader的大小最好<32k一定要<64k,规定Blob的大小最好<16m必须<32m==


```
message PrimitiveBlock {
  required StringTable stringtable = 1;
  repeated PrimitiveGroup primitivegroup = 2;

  // Granularity, units of nanodegrees, used to store coordinates in this block
  optional int32 granularity = 17 [default=100]; 
  // Offset value between the output coordinates coordinates and the granularity grid in unites of nanodegrees.
  optional int64 lat_offset = 19 [default=0];
  optional int64 lon_offset = 20 [default=0]; 

// Granularity of dates, normally represented in units of milliseconds since the 1970 epoch.
  optional int32 date_granularity = 18 [default=1000]; 


  // Proposed extension:
  //optional BBox bbox = XX;
}
```

类型为OSMData的fileblock中Blob携带PrimitiveBlock message用来保存OSM实体数据, 一般一个PrimitiveBlock携带8k实体.


fileblock中所有的字符串(key, val, role, user)都被保存在一个列表中,列表的第一个位置被保留给其它用途,在需要使用字符串的地方使用索引.


虽然不是必须的,但是将高频字符串排列在低索引的地方对于性能有帮助.(频率相同则按照字典顺序排列,可以提高压缩率)


```
latitude = .000000001 * (lat_offset + (granularity * lat))
longitude = .000000001 * (lon_offset + (granularity * lon))
```

计算经纬度的公式, 在fileblock中保存lon和lat, 然后根据granularity算出真正的nano degree, 如果有偏移值再加上偏移值,最后转回degree.

偏移值的存在是为了简洁地表达格网坐标, 通过偏移将格网坐标移动到整数,然后就可以使用比较大的granularity来节省空间.(offset的单位是nano degree它的目的是将坐标调整为整数的nano degree)


```
millisec_stamp = timestamp*date_granularity.
```

时间计算公式,代表从epoch开始的ms.


```
message PrimitiveGroup {
  repeated Node     nodes = 1;
  optional DenseNodes dense = 2;
  repeated Way      ways = 3;
  repeated Relation relations = 4;
  repeated ChangeSet changesets = 5;
}
```

PrimitiveGroup message内部可以保存多个node或者多个way或者多个relation或者多个changeset或者一个densenode, 但是只能是一种不能是多种, 原因是, 如果使用多种, protobuf无法保证写入的顺序和读的顺序一致.




```
message Way {
   required int64 id = 1;
   // Parallel arrays.
   repeated uint32 keys = 2 [packed = true];
   repeated uint32 vals = 3 [packed = true];

   optional Info info = 4;

   repeated sint64 refs = 8 [packed = true];  // DELTA coded
}

message Relation {
  enum MemberType {
    NODE = 0;
    WAY = 1;
    RELATION = 2;
  } 
   required int64 id = 1;

   // Parallel arrays.
   repeated uint32 keys = 2 [packed = true];
   repeated uint32 vals = 3 [packed = true];

   optional Info info = 4;

   // Parallel arrays
   repeated int32 roles_sid = 8 [packed = true]; // This should have been defined as uint32 for consistency, but it is now too late to change it
   repeated sint64 memids = 9 [packed = true]; // DELTA encoded
   repeated MemberType types = 10 [packed = true];
}
```

利用实体id具有连续的特性, 在way和relation中实体id使用delta encoding,来使得数值变小,节省空间.

x1, x2-x1, x3-x2 ....


```
/* Used to densly represent a sequence of nodes that do not have any tags.

We represent these nodes columnwise as five columns: ID's, lats, and
lons, all delta coded. When metadata is not omitted, 

We encode keys & vals for all nodes as a single array of integers
containing key-stringid and val-stringid, using a stringid of 0 as a
delimiter between nodes.

   ( (<keyid> <valid>)* '0' )*
 */

message DenseNodes {
   repeated sint64 id = 1 [packed = true]; // DELTA coded

   //repeated Info info = 4;
   optional DenseInfo denseinfo = 5;

   repeated sint64 lat = 8 [packed = true]; // DELTA coded
   repeated sint64 lon = 9 [packed = true]; // DELTA coded

   // Special packing of keys and vals into one array. May be empty if all nodes in this block are tagless.
   repeated int32 keys_vals = 10 [packed = true]; 
}
```


dense node的id, lon, lat使用delta编码


键值对索引编码在keys_vals列表中, 用0索引分开不同的node,

k1_1, v1_1 ,k1_2,v1_2, 0, k2_1,v2_1,k2_2,v_2_2...







