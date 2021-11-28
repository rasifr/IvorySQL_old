set datestyle = ISO ,YMD;
set timezone = 'Asia/Shanghai';
set intervalstyle = postgres;
set compatible_mode = 'oracle';

select oracle.bin_to_num(1,0,1);
select oracle.bin_to_num('1.3'::text,'1.2'::name);
select oracle.bin_to_num('1'::bpchar);
select oracle.bin_to_num('3'::bpchar);
select oracle.bin_to_num(1.2::numeric);
select oracle.bin_to_num(1.2::float8);
select oracle.bin_to_num(1.2::real,2.3::numeric);
select oracle.bin_to_num(1.2::real,1.5::numeric);
select oracle.bin_to_num(1.2::float8,1.3::float4);
select oracle.bin_to_num(1.2::float8,1::int4);
select oracle.bin_to_num(3.2::float8,1::int4);
select oracle.bin_to_num(NULL);
select oracle.bin_to_num(NULL,NULL,NULL);

select oracle.to_binary_double(2.5555::float8);
select oracle.to_binary_double(2.5555::real);
select oracle.to_binary_double('2'::oracle.varchar2);
select oracle.to_binary_double(oracle.to_char(2.55555));
select oracle.to_binary_double('1.2');
select oracle.to_binary_double('1.2'::text);
select oracle.to_binary_double('1.2'::name);
select oracle.to_binary_double(1.2::numeric);
select oracle.to_binary_double(123456789123456789.45566::numeric);
select oracle.to_binary_double(1.234567891e+200::numeric);
select oracle.to_binary_double(1.234567891e+500::float);
select oracle.to_binary_double(NULL);

select oracle.to_binary_float(2.5555::float8);
select oracle.to_binary_float(2.5555::real);
select oracle.to_binary_float('1'::oracle.varchar2);
select oracle.to_binary_float('123'::text);
select oracle.to_binary_float('1.2'::name);
select oracle.to_binary_float(1.2::numeric);
select oracle.to_binary_float(1.234567891e+200::numeric);
select oracle.to_binary_float(NULL);

select oracle.hex_to_decimal('11');
select oracle.hex_to_decimal('0xffff');
select oracle.hex_to_decimal('0xFFff');
select oracle.hex_to_decimal('0x7fffffffffffffff');
select oracle.hex_to_decimal('0x8fffffffffffffff');
select oracle.hex_to_decimal(NULL);

select oracle.to_timestamp_tz('2020-01-15');
select oracle.to_timestamp_tz('2020-01-15'::text);
select oracle.to_timestamp_tz('2019','yyyy');
select oracle.to_timestamp_tz('2019-11','yyyy-mm');
select oracle.to_timestamp_tz('2003/12/13 10:13:18 -8:00');
select oracle.to_timestamp_tz('2003/12/13 10:13:18 +1:00');
select oracle.to_timestamp_tz('2003/12/13 10:13:18 +7:00');
select oracle.to_timestamp_tz('2019-11-25'::text,'yyyy-mm-dd HH24:MI:SS OF');
select oracle.to_timestamp_tz('2019-11-25 13:38:00'::text,'yyyy-mm-dd HH24:MI:SS');
select oracle.to_timestamp_tz('2019-11-29 14:17:00','YYYY-MM-DD HH24:MI:SS TZH:TZM');
select oracle.to_timestamp_tz('2003/12/13 10:13:18 -8:00', 'YYYY/MM/DD HH:MI:SS TZH:TZM');
select oracle.to_timestamp_tz('2019/12/13 10:13:18 -2:00', 'YYYY/MM/DD HH:MI:SS TZH:TZM');
select oracle.to_timestamp_tz('2019/12/13 10:13:18 +5:00', 'YYYY/MM/DD HH:MI:SS TZH:TZM');
select oracle.to_timestamp_tz(NULL);

select oracle.to_timestamp(-12124579);
select oracle.to_timestamp(-1212457999999999999999999);
select oracle.to_timestamp(12121212.55::float);
select oracle.to_timestamp(1212121212.55);
select oracle.to_timestamp(1212121212.55::real);
select oracle.to_timestamp(NULL);
select oracle.to_timestamp(1212121212.55::numeric);
select oracle.to_timestamp('2019-11-29 14:17:00','YYYY-MM-DD HH24:MI:SS TZH:TZM');
select oracle.to_timestamp('2020/03/03 10:13:18 -8:00', 'YYYY/MM/DD HH:MI:SS TZH:TZM');
select oracle.to_timestamp('2020/03/03 10:13:18 -2:00', 'YYYY/MM/DD HH:MI:SS TZH:TZM');
select oracle.to_timestamp('2020/03/03 10:13:18 +5:00', 'YYYY/MM/DD HH:MI:SS TZH:TZM');
select oracle.to_timestamp(NULL,NULL);
select oracle.to_timestamp(1212457898999999999999);

set compatible_mode to default;
