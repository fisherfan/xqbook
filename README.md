# xqbook（尚未完成）
中国象棋及其变种棋类开局库格式

# 优点：
1.基于sqlite3标准格式，方便二次开发以及第三方工具进行修改<br />
2.局面可逆，库中保存的局面key可还原成fen<br />
3.局面使用固定方向保存，只需一次查询即可得到4种镜像局面的结果，不需要多次镜像查询<br />

# 目前已知缺点：
1.由于局面保存了所有信息，占用空间较大，尤其是给Key列建立索引后体积进一步增大。有一个想法是从Key中拿取8字节作为索引列单独存放，但不知这8字节该如何提取才能保证足够“散列”，另外不知是否有什么成熟的sqlite压缩技术用来减小文件体积<br />

# 实现细节：
Key字段：使用blob类型存储局面，无棋格子使用0表示（占1位），有棋格子使用1+棋子编码表示（占1+4=5位），所有格子拼接在一起组成的字节数组即为局面key（如果是揭棋或翻翻棋还可继续追加被吃掉的棋子编码，不过目前只是设想，是否有必要采取如此严格的模式还有待商榷），最后一个字节可能会有未用到的位，必须为0。32个棋子都在的情况需要90+32*4=218位即27.25字节

其它字段基本和obk格式相同

新增一个information表，其中存有版本、棋种的信息

不同棋种不要共用同一库文件存储内容，因为有可能造成key冲突，建议使用不同的扩展名区分：象棋用xqb、揭棋用jqb、翻翻棋用ffqb，且information表中要写清棋种