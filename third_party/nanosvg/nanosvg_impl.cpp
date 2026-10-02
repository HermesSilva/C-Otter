/* C-Otter -- unidade de compilacao da nanosvg.
 *
 * A nanosvg e' so' cabecalhos: a implementacao sai onde as duas macros abaixo
 * estao definidas, e precisa sair em UM lugar. Este arquivo nao faz parte da
 * nanosvg -- e' o ponto de montagem dela no C-Otter. Os dois cabecalhos ao
 * lado estao intactos (commit 239e102ec2c691f2902e20ace2ed36ee4a35cfe6).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
