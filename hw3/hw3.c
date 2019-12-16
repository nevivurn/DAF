#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct data {
	// input values
	int vcnt, ecnt; // vertex, edge counts
	int *label; // i -> label
	int (*edges)[2]; // edges (a, b)

	// computed values
	int *degrees;
	int **label_freq; // label -> degree -> candidate count
	int *label_freqs; // internal frequency buffer

};
static void data_init(struct data *dp, const char *path);
static void data_destroy(struct data *dp);

struct query_vert {
	int label; // vertex label
	int degree; // number of neighbors
	int *neighbors; // neighbor array
};
struct query {
	int vcnt, ecnt; // vertex, edge counts
	struct query_vert *vertices;
	int *edges; // internal edge buffer
};
static void query_init(struct query *qp, FILE *qf);
static void query_destroy(struct query *qp);

struct dag_vert {
	struct query_vert *qv; // original vertex
	int degree; // number of reachable nodes
	int *neighbors; // neighbor array
};
struct dag {
	const struct query *q; // original query
	int root; // root vertex
	struct dag_vert *vertices;
	int *edges; // internal edge buffer
};
static void dag_init(struct dag *dag, const struct data *dp, const struct query *qp);
static void dag_destroy(struct dag *dag);
static void dag_print(const struct dag *dag, const struct data *dp);

static struct {
	const struct data *dp;
	const struct dag *dag;
} cmp_data;
static double dag_score(const struct dag_vert *dagv) {
	int l = dagv->qv->label;
	int d = dagv->qv->degree;

	return (double) cmp_data.dp->label_freq[l][d] / d;
}
static int dag_cmp(const void *a, const void *b) {
	const int *ap = a, *bp = b;

	// sorting neighbors:  least-frequent labels, then by degree
	const struct dag_vert *av = &cmp_data.dag->vertices[*ap];
	const struct dag_vert *bv = &cmp_data.dag->vertices[*bp];
	int ldiff = cmp_data.dp->label_freq[av->qv->label][0] - cmp_data.dp->label_freq[bv->qv->label][0];
	int ddiff = bv->qv->degree - av->qv->degree;
	if (ldiff)
		// this works better if reversed, for some reason. makes no sense.
		return -ldiff;
	if (ddiff)
		return ddiff;
	return  0;
}

int main(int argc, char *argv[]) {
	if (argc < 4) {
		printf("usage: %s [data] [query] [query count]\n", argv[0]);
		return 1;
	}

	struct data d = {0};
	data_init(&d, argv[1]);

	int qcnt = atoi(argv[3]);
	FILE *qf = fopen(argv[2], "r");

	for (int i = 0; i < qcnt; i++) {
		struct query q;
		query_init(&q, qf);

		struct dag dag;
		dag_init(&dag, &d, &q);
		dag_print(&dag, &d);
		dag_destroy(&dag);

		query_destroy(&q);
	}

	data_destroy(&d);
	fclose(qf);
}

static void data_init(struct data *dp, const char *path) {
	FILE *f = fopen(path, "r");

	// t [id] [vcnt]
	fscanf(f, "t%*d%d", &dp->vcnt);

	dp->label = malloc(sizeof *dp->label * dp->vcnt);
	dp->edges = malloc(sizeof *dp->edges);
	dp->degrees = calloc(sizeof *dp->degrees, dp->vcnt);

	// v [vertex] [label]
	for (int i = 0; i < dp->vcnt; i++) {
		int v, l;
		fscanf(f, " v%d%d", &v, &l);
		dp->label[v] = l;
	}

	// e [vertex] [vertex] [label]
	int ecap = 1;
	while ((fscanf(f, " e%d%d%*d",
			&dp->edges[dp->ecnt][0],
			&dp->edges[dp->ecnt][1])) != EOF) {

		dp->degrees[dp->edges[dp->ecnt][0]]++;
		dp->degrees[dp->edges[dp->ecnt][1]]++;
		dp->ecnt++;

		if (dp->ecnt == ecap) {
			ecap *= 2;
			dp->edges = realloc(dp->edges, sizeof *dp->edges * ecap);
		}
	}

	int max_label = 0, max_degree = 0;
	for (int i = 0; i < dp->vcnt; i++) {
		max_label = MAX(max_label, dp->label[i]);
		max_degree = MAX(max_degree, dp->degrees[i]);
	}
	max_label++;
	max_degree++;

	dp->label_freqs = calloc(sizeof *dp->label_freqs, max_label * max_degree);
	dp->label_freq = malloc(sizeof *dp->label_freq * max_label);
	for (int i = 0; i < max_label; i++)
		dp->label_freq[i] = &dp->label_freqs[i * max_degree];

	// just count
	for (int i = 0; i < dp->vcnt; i++)
		dp->label_freq[dp->label[i]][dp->degrees[i]]++;
	// prefix sum
	for (int i = 0; i < max_label; i++)
		for (int j = max_degree-2; j >= 0; j--)
			dp->label_freq[i][j] += dp->label_freq[i][j+1];

	fclose(f);
}

static void data_destroy(struct data *dp) {
	free(dp->label);
	free(dp->edges);
	free(dp->degrees);
	free(dp->label_freq);
	free(dp->label_freqs);
}

static void query_init(struct query *qp, FILE *f) {
	// t [id] [vcnt] [degree]
	fscanf(f, " t%*d%d%d", &qp->vcnt, &qp->ecnt);

	qp->vertices = malloc(sizeof *qp->vertices * qp->vcnt);
	qp->edges = malloc(sizeof *qp->edges * qp->ecnt);

	// [vertex] [label] [degree] [neighbors...]
	int ecur = 0;
	for (int i = 0; i < qp->vcnt; i++) {
		int v, l, d;
		fscanf(f, "%d%d%d", &v, &l, &d);

		struct query_vert *qv = &qp->vertices[v];
		qv->label = l;
		qv->degree = d;
		qv->neighbors = &qp->edges[ecur];

		for (int i = 0; i < d; i++)
			fscanf(f, "%d", &qp->edges[ecur++]);
	}
}

static void query_destroy(struct query *qp) {
	free(qp->vertices);
	free(qp->edges);
}


static void dag_init(struct dag *dag, const struct data *dp, const struct query *qp) {
	dag->q = qp;
	dag->vertices = malloc(sizeof *dag->vertices * qp->vcnt);
	dag->edges = malloc(sizeof *dag->edges * qp->ecnt/2);

	// root selection: # of data vertices with same label and >= degree / self degree
	double min_score = -1.0;
	int root = 0;
	for (int i = 0; i < qp->vcnt; i++) {
		int l = qp->vertices[i].label;
		int d = qp->vertices[i].degree;

		double score = (double) dp->label_freq[l][d] / d;
		if (min_score < 0 || score < min_score) {
			min_score = score;
			root = i;
		}
	}

	int *queue = malloc(sizeof *queue * qp->vcnt);
	int *seen = calloc(sizeof *seen, qp->vcnt); // 1: queued, 2: visited
	int head = 0, tail = 0;
	queue[head++] = root;
	seen[root] = 1;
	dag->root = root;

	int ecnt = 0;
	while (tail < head) {
		int cur = queue[tail++];
		seen[cur] = 2;

		struct dag_vert *dagv = &dag->vertices[cur];
		dagv->qv = &qp->vertices[cur];
		dagv->degree = 0;
		dagv->neighbors = &dag->edges[ecnt];
		for (int i = 0; i < dagv->qv->degree; i++) {
			int next = dagv->qv->neighbors[i];

			if (seen[next] == 2)
				continue;
			dagv->neighbors[dagv->degree++] = next;
			ecnt++;

			if (seen[next] == 1)
				continue;
			queue[head++] = next;
			seen[next] = 1;
		}
	}

	free(queue);
	free(seen);
}

static void dag_destroy(struct dag *dag) {
	free(dag->vertices);
	free(dag->edges);
}

static void dag_print(const struct dag *dag, const struct data *dp) {
	cmp_data.dp = dp;
	cmp_data.dag = dag;

	int *queue = malloc(sizeof *queue * dag->q->vcnt);
	int *seen = calloc(sizeof *seen, dag->q->vcnt);
	int head = 0, tail = 0;

	queue[head++] = dag->root;
	seen[dag->root] = 1;
	while (tail < head) {
		int cur = queue[tail++];
		printf("%d ", cur);

		struct dag_vert *dagv = &dag->vertices[cur];
		qsort(dagv->neighbors, dagv->degree, sizeof (*dagv->neighbors), dag_cmp);

		for (int i = 0; i < dagv->degree; i++) {
			int next = dagv->neighbors[i];
			if (seen[next])
				continue;
			queue[head++] = next;
			seen[next] = 1;
		}
	}
	putchar('\n');

	free(queue);
	free(seen);
}
