% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/hotspotter.R
\name{hotspotter}
\alias{hotspotter}
\title{Find hotspots of genomic events}
\usage{
hotspotter(gr.list, bw, pval = 0.05)
}
\arguments{
\item{gr.list}{A list with \code{\link{GRanges}} object containing the coordinates of the genomic events.}

\item{bw}{Bandwidth used for kernel density estimation (see \code{\link[stats]{density}}).}

\item{pval}{P-value cutoff for hotspots.}
}
\value{
A \code{\link{GRanges}} object containing coordinates of hotspots with p-values.
}
\description{
Find hotspots of genomic events by using kernel \link{density} estimation.
}
\author{
Aaron Taudt
}
