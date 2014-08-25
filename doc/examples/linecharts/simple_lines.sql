IMPORT TABLE city_temperatures
   FROM CSV 'doc/examples/data/city_temperatures.csv' HEADER;

DRAW LINE CHART;
DRAW BOTTOM AXIS;

SELECT
  'gross domestic product per country' AS series,
  temperature AS x,
  temperature AS y
FROM
  city_temperatures
WHERE city = "Berlin";
