# Copyright © 2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
# This file includes unpublished proprietary source code of Kontain Inc. The
# copyright notice above does not evidence any actual or intended publication
# of such source code. Disclosure of this source code or any related
# proprietary information is strictly prohibited without the express written
# permission of Kontain Inc.

FROM python:3.7-alpine

RUN pip install flask gunicorn
COPY app /app
WORKDIR /app
EXPOSE 8080
CMD [ "gunicorn", "--bind", "0.0.0.0:8080", "app:app" ] 