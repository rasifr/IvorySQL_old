set datestyle = ISO ,YMD;
set timezone = 'Asia/Shanghai';
set intervalstyle = postgres;

select to_char(123,'xx');
select to_char(12367777,'xxx');
select to_char(12367777,'xxxxxx');
select to_char(-1,'$9');
select oracle.to_char(22);
select oracle.to_char(99::smallint);
select oracle.to_char(-44444);
select oracle.to_char(1234567890123456::bigint);
select oracle.to_char(123.456::real);
select oracle.to_char(1234.5678::double precision);
select oracle.to_char(12345678901234567890::numeric);
select oracle.to_char(1234567890.12345);
select oracle.to_char('4.00'::numeric);
select oracle.to_char('4.0010'::numeric);
select oracle.to_char(INTERVAL '0-2' YEAR TO MONTH);
select oracle.to_char(INTERVAL '12-2' YEAR TO MONTH);
select oracle.to_char(INTERVAL '3 2:20:05.7' DAY TO SECOND);
select oracle.to_char(NULL);
select oracle.to_char('22');
