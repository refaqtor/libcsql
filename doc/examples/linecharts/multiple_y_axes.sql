IMPORT TABLE city_temperatures
   FROM CSV 'doc/examples/data/city_temperatures.csv' HEADER;

DRAW LINECHART WITH
  AXIS BOTTOM
  AXIS LEFT;

SELECT
  city AS series,
  month AS x,
  temperature AS y
FROM
  city_temperatures;
SELECT
  "fnord" AS series,
  month AS x,
  temperature * 5 AS y
FROM
  city_temperatures
WHERE
  city = "Berlin";
