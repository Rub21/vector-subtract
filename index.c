#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int get_bbox_zoom(unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2) {
	int z;
	for (z = 0; z < 28; z++) {
		int mask = 1 << (31 - z);

		if (((x1 & mask) != (x2 & mask)) ||
		    ((y1 & mask) != (y2 & mask))) {
			return z;
		}
	}

	return 28;
}

/*
 *  5 bits for zoom             (<< 59)
 * 56 bits for interspersed yx  (<< 3)
 *  3 bits for tags             (<< 0)
 */
unsigned long long encode_bbox(unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, int tags) {
	int z = get_bbox_zoom(x1, y1, x2, y2);
	long long out = ((long long) z) << 59;

	int i;
	for (i = 0; i < 28; i++) {
		long long v = ((y1 >> (31 - i)) & 1) << 1;
		v |= (x1 >> (31 - i)) & 1;
		v = v << (57 - 2 * i);

		out |= v;
	}

	return out;
}

void encode_tile(int zz, int z, unsigned int x, unsigned int y, unsigned long long *start, unsigned long long *end) {
	long long out = ((long long) zz) << 59;

	x <<= (32 - z);
	y <<= (32 - z);

	int i;
	for (i = 0; i < 28; i++) {
		long long v = ((y >> (31 - i)) & 1) << 1;
		v |= (x >> (31 - i)) & 1;
		v = v << (57 - 2 * i);

		out |= v;
	}

	*start = out;
	*end = out | (((unsigned long long) -1LL) >> (2 * z + 5));
}

void decode_bbox(unsigned long long code, int *z, unsigned int *wx, unsigned int *wy) {
	*z = code >> 59;
	*wx = 0;
	*wy = 0;

	int i;
	for (i = 0; i < 28; i++) {
		long long v = code >> (57 - 2 * i);

		*wy |= ((v & 2) >> 1) << (31 - i);
		*wx |= (v & 1) << (31 - i);
	}
}

// http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
void latlon2tile(double lat, double lon, int zoom, unsigned int *x, unsigned int *y) {
	double lat_rad = lat * M_PI / 180;
	unsigned long long n = 1LL << zoom;

	*x = n * ((lon + 180) / 360);
	*y = n * (1 - (log(tan(lat_rad) + 1/cos(lat_rad)) / M_PI)) / 2;
}

// http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
void tile2latlon(unsigned int x, unsigned int y, int zoom, double *lat, double *lon) {
	unsigned long long n = 1LL << zoom;
	*lon = 360.0 * x / n - 180.0;
	double lat_rad = atan(sinh(M_PI * (1 - 2.0 * y / n)));
	*lat = lat_rad * 180 / M_PI;
}

struct point {
	unsigned long long index;
	double lat1, lon1;
	double lat2, lon2;
};

int pointcmp(const void *v1, const void *v2) {
	const unsigned long long *p1 = v1;
	const unsigned long long *p2 = v2;

	if (*p1 < *p2) {
		return -1;
	} else if (*p1 > *p2) {
		return 1;
	} else {
		return 0;
	}
}

int main(int argc, char **argv) {
	char s[2000];

	struct point *points = NULL;
	int npalloc = 0;
	int npoints = 0;

	while (fgets(s, 2000, stdin)) {
		double lat1, lon1, lat2, lon2;

		if (sscanf(s, "%lf,%lf %lf,%lf", &lat1, &lon1, &lat2, &lon2) == 4) {
			unsigned int x1, y1, x2, y2;
			latlon2tile(lat1, lon1, 32, &x1, &y1);
			latlon2tile(lat2, lon2, 32, &x2, &y2);
			int z = get_bbox_zoom(x1, y1, x2, y2);
			unsigned long long enc = encode_bbox(x1, y1, x2, y2, 0);

			if (npoints + 1 >= npalloc) {
				npalloc = (npalloc + 1000) * 3 / 2;
				points = realloc(points, npalloc * sizeof(points[0]));
			}

			points[npoints].index = enc;
			points[npoints].lat1 = lat1;
			points[npoints].lon1 = lon1;
			points[npoints].lat2 = lat2;
			points[npoints].lon2 = lon2;
			npoints++;
		}
	}

	qsort(points, npoints, sizeof(points[0]), pointcmp);

	int i;
	for (i = 0; i < npoints; i++) {
		unsigned wx, wy;
		int z;
		double lat1, lon1;

		decode_bbox(points[i].index, &z, &wx, &wy);
		tile2latlon(wx, wy, 32, &lat1, &lon1);

		printf("%llx %d %f,%f    %f,%f %f,%f\n", points[i].index, z, lat1, lon1, points[i].lat1, points[i].lon1, points[i].lat2, points[i].lon2);

		unsigned x = wx >> (32 - z);
		unsigned y = wy >> (32 - z);

		printf("\t%d/%u/%u\n", z, x, y);

		int zz;
		for (zz = 0; zz < z; zz++) {
			unsigned long long start, end;

			encode_tile(zz, zz, x >> (z - zz), y >> (z - zz), &start, &end);
			printf("\t%016llx %016llx %d %d/%u/%u\n", start, end, zz, zz, x >> (z - zz), y >> (z - zz));
		}
		for (zz = z; zz <= 28; zz++) {
			unsigned long long start, end;

			encode_tile(zz, z, x, y, &start, &end);
			printf("\t%016llx %016llx %d %d/%u/%u\n", start, end, zz, z, x, y);
		}

#if 0
		unsigned long long min = points[i].index & ~(((unsigned long long) -1LL) >> z);
		unsigned long long max = points[i].index | (((unsigned long long) -1LL) >> z);

		printf("\t%llx %llx\n", min, max);
#endif
	}
}
