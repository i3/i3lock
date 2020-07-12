/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2020 Michael Ortmann */

#ifndef _EXPLICIT_BZERO_H
#define _EXPLICIT_BZERO_H

#ifndef HAVE_EXPLICIT_BZERO
void explicit_bzero(void *const, const size_t);
#endif /* HAVE_EXPLICIT_BZERO */

#endif /* _EXPLICIT_BZERO_H */
